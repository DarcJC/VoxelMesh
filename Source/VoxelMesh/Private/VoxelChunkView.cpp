// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelChunkView.h"
#include "VoxelMeshLog.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "VoxelRenderingWorldSubsystem.h"
#include "VoxelShaders.h"

#include "IRenderCaptureProvider.h"
#include "VoxelUtilities.h"
#include "Engine/TextureRenderTarget2D.h"
#include "nanovdb/io/IO.h"
#include "VoxelMeshLog.h"

DECLARE_GPU_STAT_NAMED(FVoxelMeshGeneration, TEXT("Voxel.Mesh.Generation"));

UVoxelChunkView::UVoxelChunkView(const FObjectInitializer& ObjectInitializer)
{
	DimensionX = 1;
	DimensionY = 1;
	DimensionZ = 1;
	RHIProxy = nullptr;
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
	return VdbBulkData.IsEmpty();
}

void UVoxelChunkView::MarkAsDirty()
{
	RHIProxy = MakeShared<FVoxelChunkViewRHIProxy>(this);
}

void UVoxelChunkView::SetVdbBuffer_GameThread(nanovdb::GridHandle<nanovdb::HostBuffer>&& NewBuffer)
{
	if (NewBuffer)
	{
		nanovdb::HostBuffer& Buffer = NewBuffer.buffer();
		VdbBulkData.SetNumUninitialized(Buffer.size());
		FMemory::Memcpy(VdbBulkData.GetData(), Buffer.data(), Buffer.size());
		HostVdbBuffer = nanovdb::HostBuffer::createFull(VdbBulkData.NumBytes(), VdbBulkData.GetData());
		
		const auto& Grid = HostVdbBuffer.grid<nanovdb::Fp4>();
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
		HostVdbBuffer.reset();
		VdbBulkData.Reset();
	}
	MarkAsDirty();
}

void UVoxelChunkView::UpdateSurfaceIsoValue(float NewValue)
{
	if (NewValue != SurfaceIsoValue)
	{
		SurfaceIsoValue = NewValue;
		RebuildMesh();
	}
}

void UVoxelChunkView::Serialize(FArchive& Ar)
{
	UObject::Serialize(Ar);
	
	if (Ar.IsLoading())
	{
		if (VdbBulkData.NumBytes() > 0)
		{
			nanovdb::HostBuffer HostBuffer = nanovdb::HostBuffer::createFull(VdbBulkData.NumBytes(), VdbBulkData.GetData());
			HostVdbBuffer = nanovdb::GridHandle<nanovdb::HostBuffer>(MoveTemp(HostBuffer));
			
			MarkAsDirty();
		}
	}
}

void UVoxelChunkView::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	FName PropertyName = PropertyChangedEvent.GetMemberPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UVoxelChunkView, SurfaceIsoValue))
	{
		RebuildMesh();
	}
}

void UVoxelChunkView::RebuildMesh()
{
	GetRHIProxy()->RegenerateMesh();
}

TSharedPtr<FVoxelChunkViewRHIProxy> UVoxelChunkView::GetRHIProxy()
{
	return RHIProxy;
}

FVoxelChunkViewRHIProxy::FVoxelChunkViewRHIProxy(const UVoxelChunkView* ChunkView)
	: Parent(const_cast<UVoxelChunkView*>(ChunkView))
	, VoxelSizeX(ChunkView->DimensionX)
	, VoxelSizeY(ChunkView->DimensionY)
	, VoxelSizeZ(ChunkView->DimensionZ)
	, SurfaceIsoValue(ChunkView->SurfaceIsoValue)
	, bIsReady(true)
{
	check(IsValid(ChunkView) && !ChunkView->HostVdbBuffer.isEmpty());
	// const nanovdb::HostBuffer& HostBuffer = ChunkView->HostVdbBuffer.buffer();
	// const nanovdb::NanoGrid<float>* GridData = ChunkView->HostVdbBuffer.grid<float>();
	// const uint64_t GridByteSize = ChunkView->HostVdbBuffer.size();
	VoxelDataBuffer = ChunkView->VdbBulkData;
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

static TAutoConsoleVariable<int32> CVarVoxelMeshGenerationComputeDebug(
	TEXT("voxel.MeshGenerationComputeDebug"),
	0,
	TEXT("Enable voxel mesh debug mode\n")
	TEXT("0: off\n")
	TEXT("1: on\n"),
	ECVF_RenderThreadSafe);

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

    // Calculate the total number of cubes using all three dimensions
    const size_t TotalCubes = VoxelSizeX * VoxelSizeY * VoxelSizeZ;

    // RenderDoc Capture
	if (CVarVoxelMeshGenerationComputeDebug->GetBool())
	{
		IRenderCaptureProvider::Get().BeginCapture(&RHICmdList, 0);
	}
    
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
    UniformParameters.VoxelSizeX = VoxelSizeX;
    UniformParameters.VoxelSizeY = VoxelSizeY;
    UniformParameters.VoxelSizeZ = VoxelSizeZ;
    UniformParameters.SurfaceIsoValue = SurfaceIsoValue;
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

    // Create resources for all three steps upfront to avoid waiting
    struct FVoxelProcessingResources
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
        
        // Add initial estimate - realistic estimate is usually around 10-30% of total cubes
        uint32 EstimatedNonEmptyCubes = 0;
        
        // Add helper function to check if resources are valid
        bool AreResourcesValid() const
        {
            return NonEmptyCubeLinearIdBuffer && NonEmptyCubeLinearIdBufferUAV && NonEmptyCubeLinearIdBufferSRV &&
                   NonEmptyCubeIndexBuffer && NonEmptyCubeIndexBufferUAV && NonEmptyCubeIndexBufferSRV &&
                   VertexIndexOffsetBuffer && VertexIndexOffsetBufferUAV && VertexIndexOffsetBufferSRV;
        }
    };
    
    // TODO: More realistic estimate: only allocate for ~25% of cubes initially, with min threshold
    FVoxelProcessingResources Resources;
    Resources.EstimatedNonEmptyCubes = FMath::Max<uint32>(TotalCubes, 1024u); // 50% estimate with minimum size
    
    // Create all buffer resources upfront with more realistic size estimates
    FRHIResourceCreateInfo NonEmptyCubeLinearIdBufferInfo(TEXT("NonEmptyCube LinearId"));
    Resources.NonEmptyCubeLinearIdBuffer = RHICmdList.CreateBuffer(sizeof(uint32) * Resources.EstimatedNonEmptyCubes, 
        EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess, 0, ERHIAccess::UAVMask, NonEmptyCubeLinearIdBufferInfo);
    if (!Resources.NonEmptyCubeLinearIdBuffer)
    {
        UE_LOG(LogVoxelMesh, Error, TEXT("Failed to create NonEmptyCubeLinearIdBuffer"));
        bIsReady.store(true, std::memory_order_release);
        return;
    }
    Resources.NonEmptyCubeLinearIdBufferSRV = RHICmdList.CreateShaderResourceView(Resources.NonEmptyCubeLinearIdBuffer, 
        FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));
    Resources.NonEmptyCubeLinearIdBufferUAV = RHICmdList.CreateUnorderedAccessView(Resources.NonEmptyCubeLinearIdBuffer, 
        FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));

    FRHIResourceCreateInfo NonEmptyCubeIndexBufferInfo(TEXT("NonEmptyCube CubeIndex"));
    Resources.NonEmptyCubeIndexBuffer = RHICmdList.CreateBuffer(sizeof(uint32) * Resources.EstimatedNonEmptyCubes, 
        EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess, 0, ERHIAccess::UAVMask, NonEmptyCubeIndexBufferInfo);
    if (!Resources.NonEmptyCubeIndexBuffer)
    {
        UE_LOG(LogVoxelMesh, Error, TEXT("Failed to create NonEmptyCubeIndexBuffer"));
        bIsReady.store(true, std::memory_order_release);
        return;
    }
    Resources.NonEmptyCubeIndexBufferSRV = RHICmdList.CreateShaderResourceView(Resources.NonEmptyCubeIndexBuffer, 
        FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));
    Resources.NonEmptyCubeIndexBufferUAV = RHICmdList.CreateUnorderedAccessView(Resources.NonEmptyCubeIndexBuffer, 
        FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));

    FRHIResourceCreateInfo VertexIndexOffsetBufferInfo(TEXT("Vertex Index Offsets"));
    Resources.VertexIndexOffsetBuffer = RHICmdList.CreateBuffer(2 * sizeof(uint32) * Resources.EstimatedNonEmptyCubes, 
        EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess, 0, ERHIAccess::UAVMask, VertexIndexOffsetBufferInfo);
    if (!Resources.VertexIndexOffsetBuffer)
    {
        UE_LOG(LogVoxelMesh, Error, TEXT("Failed to create VertexIndexOffsetBuffer"));
        bIsReady.store(true, std::memory_order_release);
        return;
    }
    Resources.VertexIndexOffsetBufferSRV = RHICmdList.CreateShaderResourceView(Resources.VertexIndexOffsetBuffer, 
        FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32G32_UINT));
    Resources.VertexIndexOffsetBufferUAV = RHICmdList.CreateUnorderedAccessView(Resources.VertexIndexOffsetBuffer, 
        FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32G32_UINT));
        
    // Final validation check
    if (!Resources.AreResourcesValid())
    {
        UE_LOG(LogVoxelMesh, Error, TEXT("Failed to create one or more resources for Marching Cubes algorithm"));
        bIsReady.store(true, std::memory_order_release);
        return;
    }
    
    // Step 1: Calculate cube indices (with async continuation)
    auto CalcCubeIndexCSRef = ShaderMap->GetShader<FVoxelMarchingCubesCalcCubeIndexCS>();
    FVoxelMarchingCubesCalcCubeIndexCS::FParameters CalcCubeIndexParameters{};

    CalcCubeIndexParameters.Counter = CounterBufferUAV;
    CalcCubeIndexParameters.MarchingCubeParameters = UniformParametersBuffer;
    CalcCubeIndexParameters.SrcVoxelData = GridBufferSRV;
    CalcCubeIndexParameters.OutCubeIndexOffsets = CubeIndexOffsetBufferUAV;

    const FIntVector DispatchSize = GetDispatchSize(TotalCubes);
    FComputeShaderUtils::Dispatch(RHICmdList, CalcCubeIndexCSRef, CalcCubeIndexParameters, DispatchSize);
    
    // Use a UAV barrier instead of a fence to ensure the previous dispatch is complete
	RHICmdList.Transition(FRHITransitionInfo{CubeIndexOffsetBufferUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute});
	RHICmdList.Transition(FRHITransitionInfo{CounterBufferUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute});
    
    // Step 2: Prefix Sum and resource preparation
    auto PrefixSumCSRef = ShaderMap->GetShader<FVoxelMarchingCubesCalcCubeOffsetCS>();
    FVoxelMarchingCubesCalcCubeOffsetCS::FParameters PrefixSumParameters{};
    
    PrefixSumParameters.Counter = CounterBufferUAV;
    PrefixSumParameters.MarchingCubeParameters = UniformParametersBuffer;
    PrefixSumParameters.SrcVoxelData = GridBufferSRV;
    PrefixSumParameters.InCubeIndexOffsets = CubeIndexOffsetBufferSRV;
    PrefixSumParameters.OutNonEmptyCubeLinearId = Resources.NonEmptyCubeLinearIdBufferUAV;
    PrefixSumParameters.OutNonEmptyCubeIndex = Resources.NonEmptyCubeIndexBufferUAV;
    PrefixSumParameters.OutVertexIndexOffset = Resources.VertexIndexOffsetBufferUAV;

    FComputeShaderUtils::Dispatch(RHICmdList, PrefixSumCSRef, PrefixSumParameters, DispatchSize);
    
    // Use UAV barriers instead of fences
	RHICmdList.Transition(FRHITransitionInfo{Resources.NonEmptyCubeLinearIdBufferUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute});
	RHICmdList.Transition(FRHITransitionInfo{Resources.NonEmptyCubeIndexBufferUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute});
	RHICmdList.Transition(FRHITransitionInfo{Resources.VertexIndexOffsetBufferUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute});
	RHICmdList.Transition(FRHITransitionInfo{CounterBufferUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute});

    // Check if we should read back counter and allocate exact buffers (Memory Optimized mode)
    if (Parent && Parent->GetGenerationMode() == EVoxelMeshGenerationMode::MemoryOptimized)
    {
        // Create a fence to ensure GPU work is complete before reading counter
        FRHIGPUFence* Fence = RHICreateGPUFence(TEXT("VoxelMeshCounterReadbackFence"));
        RHICmdList.EnqueueLambda([Fence](FRHICommandListImmediate& ImmCmdList)
        {
            ImmCmdList.WriteGPUFence(Fence);
        });
        
        // Wait for GPU to finish
        Fence->Wait(RHICmdList, FRHIGPUMask::All());
        Fence->Clear();
        
        // Read counter buffer to get number of non-empty cubes
        uint32 NumNonEmptyCubes = *(static_cast<uint32*>(RHICmdList.LockBuffer(CounterBuffer, 0, sizeof(uint32), RLM_ReadOnly)));
        RHICmdList.UnlockBuffer(CounterBuffer);
        
        // Calculate required buffer sizes based on actual count
        // In marching cubes, each cube can generate up to 5 triangles (15 indices) and 12 vertices in worst case
        uint32 RequiredVertexCount = NumNonEmptyCubes * 12;
        uint32 RequiredIndexCount = NumNonEmptyCubes * 15 * 3;
        
        // Create exact-sized buffers
        ResizeBuffer_RenderThread(RequiredVertexCount * sizeof(FVector4f), RequiredIndexCount * sizeof(uint32));
        
        // Reset the counter for the final pass
        RHICmdList.ClearUAVUint(CounterBufferUAV, FUintVector4(0, 0, 0, 0));
        
        // Update estimated count for the generate mesh pass
        Resources.EstimatedNonEmptyCubes = NumNonEmptyCubes;
        
        UE_LOG(LogVoxelMesh, Log, TEXT("Memory optimized mode: Created buffers for %u non-empty cubes (%.2f%% of total)"), 
               NumNonEmptyCubes, (float)NumNonEmptyCubes / (float)TotalCubes * 100.0f);
    }
    else
    {
        // Performance optimized mode - use maximum buffer sizes
        uint32 EstimatedMaxVertices = TotalCubes * 12; // Worst case: 12 vertices per cube
        uint32 EstimatedMaxIndices = TotalCubes * 15 * 3; // Worst case: 15 triangles (45 indices) per cube
        
        ResizeBuffer_RenderThread(EstimatedMaxVertices * sizeof(FVector4f), EstimatedMaxIndices * sizeof(uint32));
        
        UE_LOG(LogVoxelMesh, Log, TEXT("Performance optimized mode: Created maximum buffer size for %llu cubes"), TotalCubes);
    }

    // Step 3: Generate Mesh
    FVoxelMarchingCubesGenerateMeshCS::FParameters GenerateMeshParameter;
    
    GenerateMeshParameter.NumNonEmptyCubes = Resources.EstimatedNonEmptyCubes;
    GenerateMeshParameter.InNonEmptyCubeIndex = Resources.NonEmptyCubeIndexBufferSRV;
    GenerateMeshParameter.InNonEmptyCubeLinearId = Resources.NonEmptyCubeLinearIdBufferSRV;
    GenerateMeshParameter.InVertexIndexOffset = Resources.VertexIndexOffsetBufferSRV;
    GenerateMeshParameter.OutVertexBuffer = MeshVertexBufferUAV;
    GenerateMeshParameter.OutIndexBuffer = MeshIndexBufferUAV;
    GenerateMeshParameter.MarchingCubeParameters = UniformParametersBuffer;
    GenerateMeshParameter.SrcVoxelData = GridBufferSRV;
    GenerateMeshParameter.InCubeIndexOffsets = CubeIndexOffsetBufferSRV;
    
    auto GenerateMeshCSRef = ShaderMap->GetShader<FVoxelMarchingCubesGenerateMeshCS>();
    FComputeShaderUtils::Dispatch(RHICmdList, GenerateMeshCSRef, GenerateMeshParameter, DispatchSize);

    // Notify finished building after the final dispatch
    ENQUEUE_RENDER_COMMAND(NotifyMeshReady)([this](FRHICommandListImmediate& RHICmdList) {
        if (const UVoxelChunkView* VoxelChunkView = Parent.Get())
        {
            VoxelChunkView->OnBuildFinished.Broadcast();
        }
        bIsReady.store(true, std::memory_order_release);
    });

    // End RenderDoc Capture
	if (CVarVoxelMeshGenerationComputeDebug->GetBool())
	{
		IRenderCaptureProvider::Get().EndCapture(&RHICmdList);
	}
}

void FVoxelChunkViewRHIProxy::RegenerateMesh_GameThread()
{
	if (IsValid(Parent))
	{
		SurfaceIsoValue = Parent->SurfaceIsoValue;
	}
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
