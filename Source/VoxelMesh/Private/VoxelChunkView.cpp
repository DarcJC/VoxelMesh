// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelChunkView.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "VoxelRenderingWorldSubsystem.h"
#include "VoxelShaders.h"

#include "IRenderCaptureProvider.h"
#include "Engine/TextureRenderTarget2D.h"
#include "nanovdb/io/IO.h"

DECLARE_GPU_STAT_NAMED(FVoxelMeshGeneration, TEXT("Voxel.Mesh.Generation"));

UVoxelChunkView::UVoxelChunkView(const FObjectInitializer& ObjectInitializer)
{
	DimensionX = 1;
	DimensionY = 1;
	DimensionZ = 1;
	RHIProxy = nullptr;
	HostVdbBuffer = nanovdb::tools::createLevelSetSphere();
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
	//
	// uint64 VoxelDataSize = 0;
	//
	// if (Ar.IsLoading())
	// {
	// 	Ar << VoxelDataSize;
	// 	
	// 	if (VoxelDataSize > 0)
	// 	{
	// 		TArray<uint8> TempData;
	// 		TempData.SetNumUninitialized(VoxelDataSize);
	// 		Ar.Serialize(TempData.GetData(), VoxelDataSize);
	// 		nanovdb::HostBuffer HostBuffer = nanovdb::HostBuffer::createFull(TempData.NumBytes(), TempData.GetData());
	// 		HostVdbBuffer = nanovdb::GridHandle<nanovdb::HostBuffer>(MoveTemp(HostBuffer));
	// 		
	// 		MarkAsDirty();
	// 	}
	// }
	// else if (Ar.IsSaving())
	// {
	// 	nanovdb::HostBuffer& Buffer = HostVdbBuffer.buffer();
	// 	VoxelDataSize = Buffer.size();
	// 	
	// 	Ar << VoxelDataSize;
	//
	// 	if (VoxelDataSize > 0)
	// 	{
	// 		Ar.Serialize(Buffer.data(), VoxelDataSize);
	// 	}
	// }
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
	const nanovdb::HostBuffer& HostBuffer = ChunkView->HostVdbBuffer.buffer();
	VoxelDataBuffer.SetNumUninitialized(HostBuffer.size() / VoxelDataBuffer.GetTypeSize());
	FMemory::Memcpy(VoxelDataBuffer.GetData(), HostBuffer.data(), HostBuffer.size());
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
	
	FRDGBuilder GraphBuilder(RHICmdList);
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	check(ShaderMap);

	const size_t TotalCubes = VoxelSize * VoxelSize * VoxelSize;

#if VOXELMESH_ENABLE_COMPUTE_DEBUG
	// RenderDoc Capture
	FRDGPassRef BeginCapturePass = GraphBuilder.AddPass(
		RDG_EVENT_NAME("BeginCapture"),
		ERDGPassFlags::None,
		[] (FRHICommandListImmediate& RHICommandListLocal)
	{
		IRenderCaptureProvider::Get().BeginCapture(&RHICommandListLocal, IRenderCaptureProvider::ECaptureFlags_Launch);
	});
#endif
	
	// Atomic counter buffer
	static constexpr uint32 DEFAULT_COUNTER_VALUES[] { 0, 0, 0, 0 };
	FRDGBufferDesc Desc = FRDGBufferDesc::CreateUploadDesc(sizeof(uint32), 4);
	Desc.Usage =  EBufferUsageFlags::Static | EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::KeepCPUAccessible;
	FRDGBufferRef CounterBuffer = GraphBuilder.CreateBuffer(Desc, TEXT("Voxel Atomic Counter"));
	GraphBuilder.QueueBufferUpload(CounterBuffer, DEFAULT_COUNTER_VALUES, sizeof(DEFAULT_COUNTER_VALUES));
	FRDGBufferUAVRef CounterBufferUAV = GraphBuilder.CreateUAV(CounterBuffer, EPixelFormat::PF_R32_UINT);

	// Uniform buffer
	FVoxelMarchingCubeUniformParameters* UniformParameters =GraphBuilder.AllocParameters<FVoxelMarchingCubeUniformParameters>();
	UniformParameters->VoxelSize = VoxelSize;
	UniformParameters->SurfaceIsoValue = 0.f;
	UniformParameters->TotalCubes = (UniformParameters->VoxelSize * UniformParameters->VoxelSize * UniformParameters->VoxelSize);
	TRDGUniformBufferRef<FVoxelMarchingCubeUniformParameters> UniformParametersBuffer = GraphBuilder.CreateUniformBuffer(UniformParameters);

	// Nanovdb data buffer
	FRDGBufferDesc GridBufferDesc = FRDGBufferDesc::CreateUploadDesc(1, VoxelDataBuffer.NumBytes());
	FRDGBufferRef GridBuffer = GraphBuilder.CreateBuffer(GridBufferDesc, TEXT("Voxel Data Buffer"));
	GraphBuilder.QueueBufferUpload(GridBuffer, VoxelDataBuffer.GetData(), VoxelDataBuffer.NumBytes());
	FRDGBufferSRVRef GridBufferSRV = GraphBuilder.CreateSRV(GridBuffer, EPixelFormat::PF_R32_UINT);

	// Cube index offset buffer
	FRDGBufferDesc CubeIndexOffsetBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TotalCubes);
	FRDGBufferRef CubeIndexOffsetBuffer = GraphBuilder.CreateBuffer(CubeIndexOffsetBufferDesc, TEXT("Cube Index Offset"));
	FRDGBufferUAVRef CubeIndexOffsetBufferUAV = GraphBuilder.CreateUAV(CubeIndexOffsetBuffer, EPixelFormat::PF_R32_UINT);
	FRDGBufferSRVRef CubeIndexOffsetBufferSRV = GraphBuilder.CreateSRV(CubeIndexOffsetBuffer, EPixelFormat::PF_R32_UINT);

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
	FVoxelDelayedResource* DelayedResource = GraphBuilder.AllocObject<FVoxelDelayedResource>();
	
	/////////////////////////
	/// Calc Cube Index (Culling)
	/////////////////////////
	auto CalcCubeIndexCSRef = ShaderMap->GetShader<FVoxelMarchingCubesCalcCubeIndexCS>();
	auto* CalcCubeIndexParameters = GraphBuilder.AllocParameters<FVoxelMarchingCubesCalcCubeIndexCS::FParameters>();

	CalcCubeIndexParameters->Counter = CounterBufferUAV;
	CalcCubeIndexParameters->MarchingCubeParameters = UniformParametersBuffer;
	CalcCubeIndexParameters->SrcVoxelData = GridBufferSRV;
	CalcCubeIndexParameters->OutCubeIndexOffsets = CubeIndexOffsetBufferUAV;
	
	FRDGPassRef CalcPass = FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Voxel Marching Cubes Calc CubeIndex"), CalcCubeIndexCSRef, CalcCubeIndexParameters, GetDispatchSize(TotalCubes));

	/////////////////////////
	/// Prefix sum
	/////////////////////////
	auto PrefixSumCSRef = ShaderMap->GetShader<FVoxelMarchingCubesCalcCubeOffsetCS>();
	auto* PrefixSumParameters = GraphBuilder.AllocParameters<FVoxelMarchingCubesCalcCubeOffsetCS::FParameters>();
	
	PrefixSumParameters->Counter = CounterBufferUAV;
	PrefixSumParameters->MarchingCubeParameters = UniformParametersBuffer;
	PrefixSumParameters->SrcVoxelData = GridBufferSRV;
	PrefixSumParameters->InCubeIndexOffsets = CubeIndexOffsetBufferSRV;

	auto* ReadCounterParameter = GraphBuilder.AllocParameters<FReadbackBufferParameters>();
	ReadCounterParameter->Buffer = CounterBuffer;

	FRDGPassRef PrefixSumResourceBindingPass = GraphBuilder.AddPass(RDG_EVENT_NAME("Voxel Marching Cubes Resource Binding"), ReadCounterParameter, ERDGPassFlags::Readback | ERDGPassFlags::NeverCull, [PrefixSumParameters, DelayedResource, TotalCubes, CounterBuffer] (FRHICommandListImmediate& RHICommandListLocal)
	{
		uint32 NumNonEmptyCubes = *(static_cast<uint32*>(RHICommandListLocal.LockBuffer(CounterBuffer->GetRHI(), 0, sizeof(uint32) * 1, RLM_ReadOnly)));
		RHICommandListLocal.UnlockBuffer(CounterBuffer->GetRHI());
		if (NumNonEmptyCubes == 0)
		{
			NumNonEmptyCubes = TotalCubes;
		}

		FRHIResourceCreateInfo NonEmptyCubeLinearIdBufferInfo(TEXT("NonEmptyCube LinearId"));
		DelayedResource->NonEmptyCubeLinearIdBuffer = RHICommandListLocal.CreateBuffer(sizeof(uint32) * NumNonEmptyCubes, EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess, 0, ERHIAccess::UAVMask, NonEmptyCubeLinearIdBufferInfo);
		DelayedResource->NonEmptyCubeLinearIdBufferSRV = RHICommandListLocal.CreateShaderResourceView(DelayedResource->NonEmptyCubeLinearIdBuffer, FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));
		DelayedResource->NonEmptyCubeLinearIdBufferUAV = RHICommandListLocal.CreateUnorderedAccessView(DelayedResource->NonEmptyCubeLinearIdBuffer, FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));

		FRHIResourceCreateInfo NonEmptyCubeIndexBufferInfo(TEXT("NonEmptyCube CubeIndex"));
		DelayedResource->NonEmptyCubeIndexBuffer = RHICommandListLocal.CreateBuffer(sizeof(uint32) * NumNonEmptyCubes, EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess, 0, ERHIAccess::UAVMask, NonEmptyCubeIndexBufferInfo);
		DelayedResource->NonEmptyCubeIndexBufferSRV = RHICommandListLocal.CreateShaderResourceView(DelayedResource->NonEmptyCubeIndexBuffer, FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));
		DelayedResource->NonEmptyCubeIndexBufferUAV = RHICommandListLocal.CreateUnorderedAccessView(DelayedResource->NonEmptyCubeIndexBuffer, FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));

		FRHIResourceCreateInfo VertexIndexOffsetBufferInfo(TEXT("Vertex Index Offsets"));
		DelayedResource->VertexIndexOffsetBuffer = RHICommandListLocal.CreateBuffer(2 * sizeof(uint32) * NumNonEmptyCubes, EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess, 0, ERHIAccess::UAVMask, VertexIndexOffsetBufferInfo);
		DelayedResource->VertexIndexOffsetBufferSRV = RHICommandListLocal.CreateShaderResourceView(DelayedResource->VertexIndexOffsetBuffer, FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32G32_UINT));
		DelayedResource->VertexIndexOffsetBufferUAV = RHICommandListLocal.CreateUnorderedAccessView(DelayedResource->VertexIndexOffsetBuffer, FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32G32_UINT));
		
		PrefixSumParameters->OutNonEmptyCubeLinearId = DelayedResource->NonEmptyCubeLinearIdBufferUAV;
		PrefixSumParameters->OutNonEmptyCubeIndex = DelayedResource->NonEmptyCubeIndexBufferUAV;
		PrefixSumParameters->OutVertexIndexOffset = DelayedResource->VertexIndexOffsetBufferUAV;

		DelayedResource->NumNonEmptyCubes = NumNonEmptyCubes;
	});

	FRDGPassRef PrefixSumPass = GraphBuilder.AddPass(RDG_EVENT_NAME("Voxel Marching Cubes Prefix Sum"), PrefixSumParameters, ERDGPassFlags::Compute | ERDGPassFlags::NeverCull, [TotalCubes, PrefixSumCSRef, PrefixSumParameters] (FRDGAsyncTask, FRHIComputeCommandList& RHICommandListLocal)
	{
		const FIntVector DispatchSize = GetDispatchSize(TotalCubes);
		FComputeShaderUtils::Dispatch(RHICommandListLocal, PrefixSumCSRef, *PrefixSumParameters, DispatchSize);
	});

	/////////////////////////
	/// Generate Mesh
	/////////////////////////
	auto* GenerateMeshParameter = GraphBuilder.AllocParameters<FVoxelMarchingCubesGenerateMeshCS::FParameters>();
	FRDGPassRef GenerateMeshResourceBindingPass = GraphBuilder.AddPass(RDG_EVENT_NAME("Voxel Generate Mesh Resource Binding"), ReadCounterParameter, ERDGPassFlags::Readback | ERDGPassFlags::NeverCull, [CounterBuffer, GenerateMeshParameter, DelayedResource, this] (FRHICommandListImmediate& RHICommandListLocal)
	{
		const uint32* CounterPtr = static_cast<uint32*>(RHICommandListLocal.LockBuffer(CounterBuffer->GetRHI(), sizeof(uint32) * 1, sizeof(uint32) * 2, RLM_ReadOnly));
		check(CounterPtr != nullptr);
		
		const uint32 NumVertices = FMath::Max(1U, *(CounterPtr + 0));
		const uint32 NumIndices = FMath::Max(1U, *(CounterPtr + 1));
		
		RHICommandListLocal.UnlockBuffer(CounterBuffer->GetRHI());

		ResizeBuffer_RenderThread(NumVertices * sizeof(FVector4f), NumIndices * sizeof(uint32));
		
		GenerateMeshParameter->NumNonEmptyCubes = DelayedResource->NumNonEmptyCubes;
		GenerateMeshParameter->InNonEmptyCubeIndex = DelayedResource->NonEmptyCubeIndexBufferSRV;
		GenerateMeshParameter->InNonEmptyCubeLinearId = DelayedResource->NonEmptyCubeLinearIdBufferSRV;
		GenerateMeshParameter->InVertexIndexOffset = DelayedResource->VertexIndexOffsetBufferSRV;
		GenerateMeshParameter->OutVertexBuffer = MeshVertexBufferUAV;
		GenerateMeshParameter->OutIndexBuffer = MeshIndexBufferUAV;
	});
	
	auto GenerateMeshCSRef = ShaderMap->GetShader<FVoxelMarchingCubesGenerateMeshCS>();
	GenerateMeshParameter->MarchingCubeParameters = UniformParametersBuffer;
	GenerateMeshParameter->SrcVoxelData = GridBufferSRV;
	GenerateMeshParameter->InCubeIndexOffsets = CubeIndexOffsetBufferSRV;
	FRDGPassRef GenerateMeshPass = GraphBuilder.AddPass(RDG_EVENT_NAME("Voxel Generate Mesh"), GenerateMeshParameter, ERDGPassFlags::Compute, [DelayedResource, GenerateMeshCSRef, GenerateMeshParameter, this] (FRDGAsyncTask, FRHIComputeCommandList& RHICommandListLocal)
	{
		const FIntVector DispatchSize = GetDispatchSize(DelayedResource->NumNonEmptyCubes);
		FComputeShaderUtils::Dispatch(RHICommandListLocal, GenerateMeshCSRef, *GenerateMeshParameter, DispatchSize);
		if (const UVoxelChunkView* VoxelChunkView = Parent.Get())
		{
			VoxelChunkView->OnBuildFinished.Broadcast();
		}
		bIsReady.store(true, std::memory_order_release);
	});
	
#if VOXELMESH_ENABLE_COMPUTE_DEBUG
	// End RenderDoc Capture
	FRDGPassRef EndCapturePass = GraphBuilder.AddPass(RDG_EVENT_NAME("End Capture"), ERDGPassFlags::None, [] (FRHICommandListImmediate& RHICmdList) 
	{
		IRenderCaptureProvider::Get().EndCapture(&RHICmdList);
	});
	
	GraphBuilder.AddPassDependency(BeginCapturePass, CalcPass);
	GraphBuilder.AddPassDependency(GenerateMeshPass, EndCapturePass);
#endif

	GraphBuilder.AddPassDependency(CalcPass, PrefixSumResourceBindingPass);;
	GraphBuilder.AddPassDependency(PrefixSumResourceBindingPass, PrefixSumPass);
	GraphBuilder.AddPassDependency(PrefixSumPass, GenerateMeshResourceBindingPass);
	GraphBuilder.AddPassDependency(GenerateMeshResourceBindingPass, GenerateMeshPass);

	GraphBuilder.Execute();
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
