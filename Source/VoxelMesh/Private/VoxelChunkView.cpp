// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelChunkView.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "VoxelRenderingWorldSubsystem.h"
#include "VoxelShaders.h"

#include "IRenderCaptureProvider.h"
#include "VoxelUtilities.h"
#include "Engine/TextureRenderTarget2D.h"
#include "nanovdb/io/IO.h"

DECLARE_GPU_STAT_NAMED(FVoxelMeshGeneration, TEXT("Voxel.Mesh.Generation"));

UVoxelChunkView::UVoxelChunkView(const FObjectInitializer& ObjectInitializer)
{
	DimensionX = 1;
	DimensionY = 1;
	DimensionZ = 1;
	RHIProxy = nullptr;
	SetVdbBuffer_GameThread(nanovdb::tools::createLevelSetSphere());
}

UVoxelChunkView::~UVoxelChunkView()
{
}

bool UVoxelChunkView::IsDirty() const
{
	return RHIProxy.IsValid();
}

bool UVoxelChunkView::IsEmpty() const
{
	return HostVdbBuffer.isEmpty();
}

void UVoxelChunkView::MarkAsDirty()
{
	RHIProxy = MakeShared<FVoxelChunkViewRHIProxy>(this);
}

void UVoxelChunkView::SetVdbBuffer_GameThread(nanovdb::GridHandle<nanovdb::HostBuffer>&& NewBuffer)
{
	HostVdbBuffer = MoveTemp(NewBuffer);
	if (HostVdbBuffer)
	{
		const auto& Grid = HostVdbBuffer.grid<float>();
		const auto& Bbox = Grid->indexBBox();
		const auto& BboxMin = Bbox.min();
		const auto& BboxMax = Bbox.max();
		DimensionX = BboxMax.x() - BboxMin.x() + 1;
		DimensionY = BboxMax.y() - BboxMin.y() + 1;
		DimensionZ = BboxMax.z() - BboxMin.z() + 1;
	}
	else
	{
		DimensionX = 0;
		DimensionY = 0;
		DimensionZ = 0;
	}
	MarkAsDirty();
}

void UVoxelChunkView::Serialize(FArchive& Ar)
{
	UObject::Serialize(Ar);
	
	uint64 VoxelDataSize = 0;
	
	if (Ar.IsLoading())
	{
		Ar << VoxelDataSize;
		
		if (VoxelDataSize > 0)
		{
			TArray<uint8> TempData;
			TempData.SetNumUninitialized(VoxelDataSize);
			Ar.Serialize(TempData.GetData(), VoxelDataSize);
			nanovdb::HostBuffer HostBuffer = nanovdb::HostBuffer::createFull(TempData.NumBytes(), TempData.GetData());
			HostVdbBuffer = nanovdb::GridHandle<nanovdb::HostBuffer>(MoveTemp(HostBuffer));
			
			MarkAsDirty();
		}
	}
	else if (Ar.IsSaving())
	{
		nanovdb::HostBuffer& Buffer = HostVdbBuffer.buffer();
		VoxelDataSize = Buffer.size();
		
		Ar << VoxelDataSize;
	
		if (VoxelDataSize > 0)
		{
			Ar.Serialize(Buffer.data(), VoxelDataSize);
		}
	}
}

void UVoxelChunkView::TestDispatch()
{
}

void UVoxelChunkView::RebuildMesh()
{
	GetRHIProxy()->RegenerateMesh();
}

TSharedPtr<FVoxelChunkViewRHIProxy> UVoxelChunkView::GetRHIProxy()
{
	return RHIProxy;
}

FVoxelChunkViewRHIProxy::FVoxelChunkViewRHIProxy(UVoxelChunkView* ChunkView)
	: Parent(ChunkView)
	, VoxelSize(FMath::Max3(ChunkView->DimensionX, ChunkView->DimensionY, ChunkView->DimensionZ))
	, bIsReady(true)
{
	check(ChunkView && !ChunkView->HostVdbBuffer.isEmpty());
	// const nanovdb::HostBuffer& HostBuffer = ChunkView->HostVdbBuffer.buffer();
	const nanovdb::NanoGrid<float>* GridData = ChunkView->HostVdbBuffer.grid<float>();
	const uint64_t GridByteSize = ChunkView->HostVdbBuffer.size();
	VoxelDataBuffer.SetNumUninitialized(GridByteSize / VoxelDataBuffer.GetTypeSize());
	FMemory::Memcpy(VoxelDataBuffer.GetData(), GridData, GridByteSize);
}

void FVoxelChunkViewRHIProxy::ResizeBuffer_RenderThread(uint32_t NewVBSize, uint32 NewIBSize)
{
	check(IsInParallelRenderingThread());

	FRHICommandList& RHICmdList = FRHICommandListImmediate::Get();

	if (!MeshVertexBuffer || MeshVertexBuffer->GetSize() != NewVBSize)
	{
		FRHIResourceCreateInfo BufferCreateInfo(TEXT("Voxel Vertex Buffer"));
		MeshVertexBuffer = RHICmdList.CreateVertexBuffer(NewVBSize, EBufferUsageFlags::UnorderedAccess, BufferCreateInfo);
		MeshVertexBufferUAV = RHICmdList.CreateUnorderedAccessView(MeshVertexBuffer, PF_R32G32B32A32_UINT);
	}

	if (!MeshIndexBuffer || MeshIndexBuffer->GetSize() != NewIBSize)
	{
		FRHIResourceCreateInfo BufferCreateInfo(TEXT("Voxel Index Buffer"));
		MeshIndexBuffer = RHICmdList.CreateIndexBuffer(sizeof(uint32), NewIBSize, EBufferUsageFlags::UnorderedAccess, BufferCreateInfo);
		MeshIndexBufferUAV = RHICmdList.CreateUnorderedAccessView(MeshIndexBuffer, PF_R32_UINT);
	}
}

#define VOXELMESH_ENABLE_COMPUTE_DEBUG 0

void FVoxelChunkViewRHIProxy::RegenerateMesh_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	if (bool Expected = true; !bIsReady.compare_exchange_strong(Expected, false))
	{
		return;
	}
	SCOPED_GPU_STAT(RHICmdList, FVoxelMeshGeneration);
	RHI_BREADCRUMB_EVENT(RHICmdList, "VoxelMeshGeneration");
	
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	check(ShaderMap);

	const size_t TotalCubes = VoxelSize * VoxelSize * VoxelSize;

#if VOXELMESH_ENABLE_COMPUTE_DEBUG
	// RenderDoc Capture
	IRenderCaptureProvider::Get().BeginCapture(&RHICmdList, 0);
#endif
	
	// Atomic counter buffer
	static constexpr uint32 DEFAULT_COUNTER_VALUES[] { 0, 0, 0, 0 };
	FRHIResourceCreateInfo Desc(TEXT("VoxelMeshCounter"));
	FBufferRHIRef CounterBuffer = RHICmdList.CreateBuffer(sizeof(DEFAULT_COUNTER_VALUES), EBufferUsageFlags::Static | EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::KeepCPUAccessible, 0, ERHIAccess::UAVMask, Desc);
	uint32* StagingPtr = static_cast<uint32*>(RHICmdList.LockBuffer(CounterBuffer, 0, sizeof(DEFAULT_COUNTER_VALUES), RLM_WriteOnly));
	FMemory::Memcpy(StagingPtr, DEFAULT_COUNTER_VALUES, sizeof(DEFAULT_COUNTER_VALUES));
	RHICmdList.UnlockBuffer(CounterBuffer);
	FUnorderedAccessViewRHIRef CounterBufferUAV = RHICmdList.CreateUnorderedAccessView(CounterBuffer, FRHIViewDesc::CreateBufferUAV().SetTypeFromBuffer(CounterBuffer).SetFormat(PF_R32_UINT));

	// Uniform buffer
	FVoxelMarchingCubeUniformParameters UniformParameters;
	UniformParameters.VoxelSize = VoxelSize;
	UniformParameters.SurfaceIsoValue = 0.f;
	UniformParameters.TotalCubes = TotalCubes;
	TUniformBufferRef<FVoxelMarchingCubeUniformParameters> UniformParametersBuffer = CreateUniformBufferImmediate(UniformParameters, UniformBuffer_SingleFrame);

	// Nanovdb data buffer
	FRHIResourceCreateInfo UniformBufferCreateInfo(TEXT("VoxelMeshGridBuffer"));
	FBufferRHIRef GridBuffer = RHICmdList.CreateStructuredBuffer(sizeof(uint32), VoxelDataBuffer.NumBytes(), EBufferUsageFlags::ShaderResource | EBufferUsageFlags::VertexBuffer, ERHIAccess::SRVMask, UniformBufferCreateInfo);
	uint8* GridStagingPtr = static_cast<uint8*>(RHICmdList.LockBuffer(GridBuffer, 0, VoxelDataBuffer.NumBytes(), RLM_WriteOnly));
	FMemory::Memcpy(GridStagingPtr, VoxelDataBuffer.GetData(), VoxelDataBuffer.NumBytes());
	RHICmdList.UnlockBuffer(GridBuffer);
	FShaderResourceViewRHIRef GridBufferSRV = RHICmdList.CreateShaderResourceView(GridBuffer, FRHIViewDesc::CreateBufferSRV().SetTypeFromBuffer(GridBuffer));

	// Cube index offset buffer
	FRDGBufferDesc CubeIndexOffsetBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TotalCubes);
	FRHIResourceCreateInfo IndexOffsetBufferCreateInfo(TEXT("VoxelMeshIndexOffsetBuffer"));
	FBufferRHIRef CubeIndexOffsetBuffer = RHICmdList.CreateBuffer(sizeof(uint32) * TotalCubes, EBufferUsageFlags::Static | EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::VertexBuffer, 0, ERHIAccess::UAVMask, IndexOffsetBufferCreateInfo);
	FShaderResourceViewRHIRef CubeIndexOffsetBufferSRV = RHICmdList.CreateShaderResourceView(CubeIndexOffsetBuffer, FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(PF_R32_UINT));
	FUnorderedAccessViewRHIRef CubeIndexOffsetBufferUAV = RHICmdList.CreateUnorderedAccessView(CubeIndexOffsetBuffer, FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(PF_R32_UINT));

	// We create these buffer later
	struct FVoxelDelayedResource
	{
		TRefCountPtr<FRHIBuffer> NonEmptyCubeLinearIdBuffer = nullptr;
		TRefCountPtr<FRHIUnorderedAccessView> NonEmptyCubeLinearIdBufferUAV = nullptr;
		TRefCountPtr<FRHIShaderResourceView> NonEmptyCubeLinearIdBufferSRV = nullptr;
		
		TRefCountPtr<FRHIBuffer> NonEmptyCubeIndexBuffer = nullptr;
		TRefCountPtr<FRHIUnorderedAccessView> NonEmptyCubeIndexBufferUAV = nullptr;
		TRefCountPtr<FRHIShaderResourceView> NonEmptyCubeIndexBufferSRV = nullptr;
		
		TRefCountPtr<FRHIBuffer> VertexIndexOffsetBuffer = nullptr;
		TRefCountPtr<FRHIUnorderedAccessView> VertexIndexOffsetBufferUAV = nullptr;
		TRefCountPtr<FRHIShaderResourceView> VertexIndexOffsetBufferSRV = nullptr;

		TRefCountPtr<FRHIUnorderedAccessView> MeshVertexBufferUAV = nullptr;
		TRefCountPtr<FRHIUnorderedAccessView> MeshIndexBufferUAV = nullptr;

		uint32 NumNonEmptyCubes = 0;
	};
	FVoxelDelayedResource DelayedResource{};
	
	/////////////////////////
	/// Calc Cube Index (Culling)
	/////////////////////////
	auto CalcCubeIndexCSRef = ShaderMap->GetShader<FVoxelMarchingCubesCalcCubeIndexCS>();
	FVoxelMarchingCubesCalcCubeIndexCS::FParameters CalcCubeIndexParameters{};

	CalcCubeIndexParameters.Counter = CounterBufferUAV;
	CalcCubeIndexParameters.MarchingCubeParameters = UniformParametersBuffer;
	CalcCubeIndexParameters.SrcVoxelData = GridBufferSRV;
	CalcCubeIndexParameters.OutCubeIndexOffsets = CubeIndexOffsetBufferUAV;

	FGPUFenceRHIRef Fence = RHICreateGPUFence(TEXT("Voxel Readback Fence"));
	FComputeShaderUtils::Dispatch(RHICmdList, CalcCubeIndexCSRef, CalcCubeIndexParameters, GetDispatchSize(TotalCubes));
	RHICmdList.WriteGPUFence(Fence);
	RHICmdList.SubmitAndBlockUntilGPUIdle();

	ENQUEUE_RENDER_COMMAND(VoxelChunkViewPrefixSum)([this, Fence, DelayedResource, ShaderMap, CounterBufferUAV, UniformParametersBuffer, GridBufferSRV, CubeIndexOffsetBufferSRV, GridBuffer, CubeIndexOffsetBuffer, CounterBuffer, TotalCubes] (FRHICommandListImmediate& RHICmdList) mutable 
	{
		/////////////////////////
		/// Prefix sum
		/////////////////////////
		auto PrefixSumCSRef = ShaderMap->GetShader<FVoxelMarchingCubesCalcCubeOffsetCS>();
		FVoxelMarchingCubesCalcCubeOffsetCS::FParameters PrefixSumParameters{};
		
		PrefixSumParameters.Counter = CounterBufferUAV;
		PrefixSumParameters.MarchingCubeParameters = UniformParametersBuffer;
		PrefixSumParameters.SrcVoxelData = GridBufferSRV;
		PrefixSumParameters.InCubeIndexOffsets = CubeIndexOffsetBufferSRV;

		Fence->Wait(RHICmdList, FRHIGPUMask::All());
		Fence->Clear();
		uint32 NumNonEmptyCubes = *(static_cast<uint32*>(RHICmdList.LockBuffer(CounterBuffer, 0, sizeof(uint32) * 1, RLM_ReadOnly)));
		RHICmdList.UnlockBuffer(CounterBuffer);
		ensureMsgf(NumNonEmptyCubes != 0, TEXT("Empty SDF or this is a bug?"));
		if (NumNonEmptyCubes == 0)
		{
			NumNonEmptyCubes = TotalCubes;
		}
		
		FRHIResourceCreateInfo NonEmptyCubeLinearIdBufferInfo(TEXT("NonEmptyCube LinearId"));
		DelayedResource.NonEmptyCubeLinearIdBuffer = RHICmdList.CreateBuffer(sizeof(uint32) * NumNonEmptyCubes, EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess, 0, ERHIAccess::UAVMask, NonEmptyCubeLinearIdBufferInfo);
		DelayedResource.NonEmptyCubeLinearIdBufferSRV = RHICmdList.CreateShaderResourceView(DelayedResource.NonEmptyCubeLinearIdBuffer, FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));
		DelayedResource.NonEmptyCubeLinearIdBufferUAV = RHICmdList.CreateUnorderedAccessView(DelayedResource.NonEmptyCubeLinearIdBuffer, FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));

		FRHIResourceCreateInfo NonEmptyCubeIndexBufferInfo(TEXT("NonEmptyCube CubeIndex"));
		DelayedResource.NonEmptyCubeIndexBuffer = RHICmdList.CreateBuffer(sizeof(uint32) * NumNonEmptyCubes, EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess, 0, ERHIAccess::UAVMask, NonEmptyCubeIndexBufferInfo);
		DelayedResource.NonEmptyCubeIndexBufferSRV = RHICmdList.CreateShaderResourceView(DelayedResource.NonEmptyCubeIndexBuffer, FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));
		DelayedResource.NonEmptyCubeIndexBufferUAV = RHICmdList.CreateUnorderedAccessView(DelayedResource.NonEmptyCubeIndexBuffer, FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));

		FRHIResourceCreateInfo VertexIndexOffsetBufferInfo(TEXT("Vertex Index Offsets"));
		DelayedResource.VertexIndexOffsetBuffer = RHICmdList.CreateBuffer(2 * sizeof(uint32) * NumNonEmptyCubes, EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess, 0, ERHIAccess::UAVMask, VertexIndexOffsetBufferInfo);
		DelayedResource.VertexIndexOffsetBufferSRV = RHICmdList.CreateShaderResourceView(DelayedResource.VertexIndexOffsetBuffer, FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32G32_UINT));
		DelayedResource.VertexIndexOffsetBufferUAV = RHICmdList.CreateUnorderedAccessView(DelayedResource.VertexIndexOffsetBuffer, FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32G32_UINT));
		
		PrefixSumParameters.OutNonEmptyCubeLinearId = DelayedResource.NonEmptyCubeLinearIdBufferUAV;
		PrefixSumParameters.OutNonEmptyCubeIndex = DelayedResource.NonEmptyCubeIndexBufferUAV;
		PrefixSumParameters.OutVertexIndexOffset = DelayedResource.VertexIndexOffsetBufferUAV;

		DelayedResource.NumNonEmptyCubes = NumNonEmptyCubes;

		{
			const FIntVector DispatchSize = GetDispatchSize(TotalCubes);
			FComputeShaderUtils::Dispatch(RHICmdList, PrefixSumCSRef, PrefixSumParameters, DispatchSize);
			RHICmdList.WriteGPUFence(Fence);
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		
		ENQUEUE_RENDER_COMMAND(VoxelGeneratingMesh)([this, Fence, DelayedResource, ShaderMap, CounterBufferUAV, UniformParametersBuffer, GridBufferSRV, CubeIndexOffsetBufferSRV, GridBuffer, CubeIndexOffsetBuffer, CounterBuffer, TotalCubes] (FRHICommandListImmediate& RHICmdList)
		{
			/////////////////////////
			/// Generate Mesh
			/////////////////////////
			FVoxelMarchingCubesGenerateMeshCS::FParameters GenerateMeshParameter;

			Fence->Wait(RHICmdList, FRHIGPUMask::All());
			const uint32* CounterPtr = static_cast<uint32*>(RHICmdList.LockBuffer(CounterBuffer, sizeof(uint32) * 1, sizeof(uint32) * 2, RLM_ReadOnly));
			check(CounterPtr != nullptr);
			
			const uint32 NumVertices = FMath::Max(1U, *(CounterPtr + 0));
			const uint32 NumIndices = FMath::Max(1U, *(CounterPtr + 1));
			
			RHICmdList.UnlockBuffer(CounterBuffer);

			ResizeBuffer_RenderThread(NumVertices * sizeof(FVector4f), NumIndices * sizeof(uint32));
			
			GenerateMeshParameter.NumNonEmptyCubes = DelayedResource.NumNonEmptyCubes;
			GenerateMeshParameter.InNonEmptyCubeIndex = DelayedResource.NonEmptyCubeIndexBufferSRV;
			GenerateMeshParameter.InNonEmptyCubeLinearId = DelayedResource.NonEmptyCubeLinearIdBufferSRV;
			GenerateMeshParameter.InVertexIndexOffset = DelayedResource.VertexIndexOffsetBufferSRV;
			GenerateMeshParameter.OutVertexBuffer = MeshVertexBufferUAV;
			GenerateMeshParameter.OutIndexBuffer = MeshIndexBufferUAV;
			
			auto GenerateMeshCSRef = ShaderMap->GetShader<FVoxelMarchingCubesGenerateMeshCS>();
			GenerateMeshParameter.MarchingCubeParameters = UniformParametersBuffer;
			GenerateMeshParameter.SrcVoxelData = GridBufferSRV;
			GenerateMeshParameter.InCubeIndexOffsets = CubeIndexOffsetBufferSRV;

			{
				const FIntVector DispatchSize = GetDispatchSize(DelayedResource.NumNonEmptyCubes);
				FComputeShaderUtils::Dispatch(RHICmdList, GenerateMeshCSRef, GenerateMeshParameter, DispatchSize);
				if (const UVoxelChunkView* VoxelChunkView = Parent.Get())
				{
					VoxelChunkView->OnBuildFinished.Broadcast();
				}
				bIsReady.store(true, std::memory_order_release);
			}
		});
	});

#if VOXELMESH_ENABLE_COMPUTE_DEBUG
	// End RenderDoc Capture
	IRenderCaptureProvider::Get().EndCapture(&RHICmdList);
#endif
}

void FVoxelChunkViewRHIProxy::RegenerateMesh_GameThread()
{
	ENQUEUE_RENDER_COMMAND(VoxelMeshMarchingCubes)([this] (FRHICommandListImmediate& RHICmdList)
	{
		RegenerateMesh_RenderThread(RHICmdList);
	});
}

void FVoxelChunkViewRHIProxy::RegenerateMesh()
{
	RegenerateMesh_GameThread();
}

bool FVoxelChunkViewRHIProxy::IsReady() const
{
	return MeshVertexBuffer && MeshIndexBuffer;
}

bool FVoxelChunkViewRHIProxy::IsGenerating() const
{
	return !bIsReady.load(std::memory_order_acquire);
}
