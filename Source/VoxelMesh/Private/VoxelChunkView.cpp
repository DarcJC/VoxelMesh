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

void UVoxelChunkView::RebuildMeshAsync()
{
	if (auto Proxy = GetRHIProxy())
	{
		UE_LOG(LogVoxelMesh, Log, TEXT("Force rebuilding mesh using async compute"));
		Proxy->RegenerateMeshAsync_GameThread();
	}
}

void UVoxelChunkView::RebuildMeshSync()
{
	if (auto Proxy = GetRHIProxy())
	{
		UE_LOG(LogVoxelMesh, Log, TEXT("Force rebuilding mesh using synchronous compute"));
		Proxy->RegenerateMesh_GameThread();
	}
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

static TAutoConsoleVariable<int32> CVarVoxelMeshUseAsyncCompute(
	TEXT("voxel.UseAsyncCompute"),
	1,
	TEXT("Use async compute for voxel mesh generation\n")
	TEXT("0: Use synchronous compute (fallback)\n")
	TEXT("1: Use async compute when supported (default)\n"),
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
	// Check if async compute is enabled and supported
	const bool bAsyncComputeEnabled = CVarVoxelMeshUseAsyncCompute.GetValueOnAnyThread() != 0;
	const bool bAsyncComputeSupported = GRHISupportsAsyncCompute && GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5;
	
	if (bAsyncComputeEnabled && bAsyncComputeSupported)
	{
		// Use async compute for better performance
		RegenerateMeshAsync_GameThread();
	}
	else
	{
		// Fall back to synchronous rendering
		if (bAsyncComputeEnabled && !bAsyncComputeSupported)
		{
			UE_LOG(LogVoxelMesh, Warning, TEXT("Async compute requested but not supported, falling back to synchronous mesh generation"));
		}
		RegenerateMesh_GameThread();
	}
}

bool FVoxelChunkViewRHIProxy::IsReady() const
{
	return MeshVertexBuffer && MeshIndexBuffer;
}

bool FVoxelChunkViewRHIProxy::IsGenerating() const
{
	return !bIsReady.load(std::memory_order_acquire) || bIsAsyncGenerating.load(std::memory_order_acquire);
}

void FVoxelChunkViewRHIProxy::RegenerateMeshAsync_GameThread()
{
	// Prevent concurrent async generation attempts
	if (bIsAsyncGenerating.load(std::memory_order_acquire))
	{
		UE_LOG(LogVoxelMesh, Warning, TEXT("Async mesh generation already in progress, ignoring new request"));
		return;
	}

	if (IsValid(Parent))
	{
		SurfaceIsoValue = Parent->SurfaceIsoValue;
	}
	
	// Record start time for performance tracking
	AsyncStartTime = FPlatformTime::Seconds();
	
	// Mark as not ready and generating (atomic operations)
	bIsReady.store(false, std::memory_order_release);
	bIsAsyncGenerating.store(true, std::memory_order_release);
	
	ENQUEUE_RENDER_COMMAND(VoxelMeshMarchingCubesAsync)([this](FRHICommandListImmediate& RHICmdList)
	{
		RegenerateMeshAsync_RenderThread();
	});
}

void FVoxelChunkViewRHIProxy::RegenerateMeshAsync_RenderThread()
{
	// Get async compute command list
	FRHIAsyncComputeCommandListImmediate& AsyncComputeCmdList = FRHICommandListExecutor::GetImmediateAsyncComputeCommandList();
	
	// Run the marching cubes algorithm on async compute queue
	RegenerateMeshAsyncCompute_RenderThread(AsyncComputeCmdList);
}

void FVoxelChunkViewRHIProxy::RegenerateMeshAsyncCompute_RenderThread(FRHIAsyncComputeCommandListImmediate& AsyncComputeCmdList)
{
	// Verify we're still in generating state (defensive check)
	if (!bIsAsyncGenerating.load(std::memory_order_acquire))
	{
		UE_LOG(LogVoxelMesh, Warning, TEXT("Async mesh generation was cancelled before render thread execution"));
		return;
	}

	// Get shader map
	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	if (!ShaderMap)
	{
		UE_LOG(LogVoxelMesh, Error, TEXT("Cannot get global shader map, cannot generate voxel mesh"));
		bIsReady.store(true, std::memory_order_release);
		bIsAsyncGenerating.store(false, std::memory_order_release);
		return;
	}

	// Begin RenderDoc Capture
	const bool bShouldCaptureRenderDoc = CVarVoxelMeshGenerationComputeDebug && CVarVoxelMeshGenerationComputeDebug->GetBool();
	if (bShouldCaptureRenderDoc)
	{
		IRenderCaptureProvider::Get().BeginCapture(&AsyncComputeCmdList, FColor::Cyan, TEXT("VoxelMeshGeneration"));
	}

	// Safety check for voxel data
	if (VoxelDataBuffer.IsEmpty())
	{
		UE_LOG(LogVoxelMesh, Error, TEXT("Voxel data buffer is empty, cannot generate mesh"));
		bIsReady.store(true, std::memory_order_release);
		bIsAsyncGenerating.store(false, std::memory_order_release);
		return;
	}

	// Safety check for mesh buffers
	if (!MeshVertexBufferUAV || !MeshIndexBufferUAV)
	{
		UE_LOG(LogVoxelMesh, Error, TEXT("Mesh buffers not initialized, cannot generate mesh"));
		bIsReady.store(true, std::memory_order_release);
		bIsAsyncGenerating.store(false, std::memory_order_release);
		return;
	}

	// Calculate dimensions and total cubes
	const uint64 TotalCubes = static_cast<uint64>(VoxelSizeX - 1) * static_cast<uint64>(VoxelSizeY - 1) * static_cast<uint64>(VoxelSizeZ - 1);
	
	// Guard against excessive computation
	if (TotalCubes > static_cast<uint64>(MAX_uint32))
	{
		UE_LOG(LogVoxelMesh, Error, TEXT("Total cubes (%llu) exceeds maximum supported value (%u)"), TotalCubes, MAX_uint32);
		bIsReady.store(true, std::memory_order_release);
		bIsAsyncGenerating.store(false, std::memory_order_release);
		return;
	}

	// Create counter buffer for atomic operations
	FRHIResourceCreateInfo CounterBufferCreateInfo(TEXT("VoxelMeshCounterBuffer"));
	FBufferRHIRef CounterBuffer = AsyncComputeCmdList.CreateBuffer(3 * sizeof(uint32), 
		EBufferUsageFlags::Static | EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::VertexBuffer, 
		0, ERHIAccess::UAVMask, CounterBufferCreateInfo);
	FUnorderedAccessViewRHIRef CounterBufferUAV = AsyncComputeCmdList.CreateUnorderedAccessView(CounterBuffer, 
		FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(PF_R32_UINT));

	// Clear counter buffer
	AsyncComputeCmdList.ClearUAVUint(CounterBufferUAV, FUintVector4(0, 0, 0, 0));

	// Uniform buffer setup
	FVoxelMarchingCubeUniformParameters UniformParameters;
	UniformParameters.VoxelSizeX = VoxelSizeX;
	UniformParameters.VoxelSizeY = VoxelSizeY;
	UniformParameters.VoxelSizeZ = VoxelSizeZ;
	UniformParameters.SurfaceIsoValue = SurfaceIsoValue;
	UniformParameters.TotalCubes = static_cast<uint32>(TotalCubes);
	TUniformBufferRef<FVoxelMarchingCubeUniformParameters> UniformParametersBuffer = CreateUniformBufferImmediate(UniformParameters, UniformBuffer_SingleFrame);

	// Create nanovdb data buffer
	FRHIResourceCreateInfo GridBufferCreateInfo(TEXT("VoxelMeshGridBuffer"));
	FBufferRHIRef GridBuffer = AsyncComputeCmdList.CreateStructuredBuffer(sizeof(uint32), VoxelDataBuffer.NumBytes(), 
		EBufferUsageFlags::ShaderResource | EBufferUsageFlags::VertexBuffer, ERHIAccess::SRVMask, GridBufferCreateInfo);
	uint8* GridStagingPtr = static_cast<uint8*>(AsyncComputeCmdList.LockBuffer(GridBuffer, 0, VoxelDataBuffer.NumBytes(), RLM_WriteOnly));
	FMemory::Memcpy(GridStagingPtr, VoxelDataBuffer.GetData(), VoxelDataBuffer.NumBytes());
	AsyncComputeCmdList.UnlockBuffer(GridBuffer);
	FShaderResourceViewRHIRef GridBufferSRV = AsyncComputeCmdList.CreateShaderResourceView(GridBuffer, 
		FRHIViewDesc::CreateBufferSRV().SetTypeFromBuffer(GridBuffer));

	// Create cube index offset buffer
	FRHIResourceCreateInfo IndexOffsetBufferCreateInfo(TEXT("VoxelMeshIndexOffsetBuffer"));
	FBufferRHIRef CubeIndexOffsetBuffer = AsyncComputeCmdList.CreateBuffer(sizeof(uint32) * TotalCubes, 
		EBufferUsageFlags::Static | EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::VertexBuffer, 
		0, ERHIAccess::UAVMask, IndexOffsetBufferCreateInfo);
	FShaderResourceViewRHIRef CubeIndexOffsetBufferSRV = AsyncComputeCmdList.CreateShaderResourceView(CubeIndexOffsetBuffer, 
		FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(PF_R32_UINT));
	FUnorderedAccessViewRHIRef CubeIndexOffsetBufferUAV = AsyncComputeCmdList.CreateUnorderedAccessView(CubeIndexOffsetBuffer, 
		FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(PF_R32_UINT));

	// Create processing resources
	FVoxelProcessingResources Resources;
	Resources.EstimatedNonEmptyCubes = FMath::Max<uint32>(static_cast<uint32>(TotalCubes), 1024u);
	
	// Create all buffer resources
	FRHIResourceCreateInfo NonEmptyCubeLinearIdBufferInfo(TEXT("NonEmptyCube LinearId"));
	Resources.NonEmptyCubeLinearIdBuffer = AsyncComputeCmdList.CreateBuffer(sizeof(uint32) * Resources.EstimatedNonEmptyCubes, 
		EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess, 0, ERHIAccess::UAVMask, NonEmptyCubeLinearIdBufferInfo);
	if (!Resources.NonEmptyCubeLinearIdBuffer)
	{
		UE_LOG(LogVoxelMesh, Error, TEXT("Failed to create NonEmptyCubeLinearIdBuffer"));
		bIsReady.store(true, std::memory_order_release);
		bIsAsyncGenerating.store(false, std::memory_order_release);
		return;
	}
	Resources.NonEmptyCubeLinearIdBufferSRV = AsyncComputeCmdList.CreateShaderResourceView(Resources.NonEmptyCubeLinearIdBuffer, 
		FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));
	Resources.NonEmptyCubeLinearIdBufferUAV = AsyncComputeCmdList.CreateUnorderedAccessView(Resources.NonEmptyCubeLinearIdBuffer, 
		FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));

	FRHIResourceCreateInfo NonEmptyCubeIndexBufferInfo(TEXT("NonEmptyCube CubeIndex"));
	Resources.NonEmptyCubeIndexBuffer = AsyncComputeCmdList.CreateBuffer(sizeof(uint32) * Resources.EstimatedNonEmptyCubes, 
		EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess, 0, ERHIAccess::UAVMask, NonEmptyCubeIndexBufferInfo);
	if (!Resources.NonEmptyCubeIndexBuffer)
	{
		UE_LOG(LogVoxelMesh, Error, TEXT("Failed to create NonEmptyCubeIndexBuffer"));
		bIsReady.store(true, std::memory_order_release);
		bIsAsyncGenerating.store(false, std::memory_order_release);
		return;
	}
	Resources.NonEmptyCubeIndexBufferSRV = AsyncComputeCmdList.CreateShaderResourceView(Resources.NonEmptyCubeIndexBuffer, 
		FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));
	Resources.NonEmptyCubeIndexBufferUAV = AsyncComputeCmdList.CreateUnorderedAccessView(Resources.NonEmptyCubeIndexBuffer, 
		FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32_UINT));

	FRHIResourceCreateInfo VertexIndexOffsetBufferInfo(TEXT("Vertex Index Offsets"));
	Resources.VertexIndexOffsetBuffer = AsyncComputeCmdList.CreateBuffer(2 * sizeof(uint32) * Resources.EstimatedNonEmptyCubes, 
		EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess, 0, ERHIAccess::UAVMask, VertexIndexOffsetBufferInfo);
	if (!Resources.VertexIndexOffsetBuffer)
	{
		UE_LOG(LogVoxelMesh, Error, TEXT("Failed to create VertexIndexOffsetBuffer"));
		bIsReady.store(true, std::memory_order_release);
		bIsAsyncGenerating.store(false, std::memory_order_release);
		return;
	}
	Resources.VertexIndexOffsetBufferSRV = AsyncComputeCmdList.CreateShaderResourceView(Resources.VertexIndexOffsetBuffer, 
		FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32G32_UINT));
	Resources.VertexIndexOffsetBufferUAV = AsyncComputeCmdList.CreateUnorderedAccessView(Resources.VertexIndexOffsetBuffer, 
		FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(EPixelFormat::PF_R32G32_UINT));
		
	if (!Resources.AreResourcesValid())
	{
		UE_LOG(LogVoxelMesh, Error, TEXT("Failed to create one or more resources for Marching Cubes algorithm"));
		bIsReady.store(true, std::memory_order_release);
		bIsAsyncGenerating.store(false, std::memory_order_release);
		return;
	}
	
	// Step 1: Calculate cube indices (async)
	auto CalcCubeIndexCSRef = ShaderMap->GetShader<FVoxelMarchingCubesCalcCubeIndexCS>();
	FVoxelMarchingCubesCalcCubeIndexCS::FParameters CalcCubeIndexParameters{};
	CalcCubeIndexParameters.Counter = CounterBufferUAV;
	CalcCubeIndexParameters.MarchingCubeParameters = UniformParametersBuffer;
	CalcCubeIndexParameters.SrcVoxelData = GridBufferSRV;
	CalcCubeIndexParameters.OutCubeIndexOffsets = CubeIndexOffsetBufferUAV;

	const FIntVector DispatchSize = GetDispatchSize(static_cast<uint32>(TotalCubes));
	FComputeShaderUtils::Dispatch(AsyncComputeCmdList, CalcCubeIndexCSRef, CalcCubeIndexParameters, DispatchSize);
	
	// Async compute UAV barriers
	AsyncComputeCmdList.Transition(FRHITransitionInfo{CubeIndexOffsetBufferUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute});
	AsyncComputeCmdList.Transition(FRHITransitionInfo{CounterBufferUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute});
	
	// Step 2: Prefix Sum (async)
	auto PrefixSumCSRef = ShaderMap->GetShader<FVoxelMarchingCubesCalcCubeOffsetCS>();
	FVoxelMarchingCubesCalcCubeOffsetCS::FParameters PrefixSumParameters{};
	PrefixSumParameters.Counter = CounterBufferUAV;
	PrefixSumParameters.MarchingCubeParameters = UniformParametersBuffer;
	PrefixSumParameters.SrcVoxelData = GridBufferSRV;
	PrefixSumParameters.InCubeIndexOffsets = CubeIndexOffsetBufferSRV;
	PrefixSumParameters.OutNonEmptyCubeLinearId = Resources.NonEmptyCubeLinearIdBufferUAV;
	PrefixSumParameters.OutNonEmptyCubeIndex = Resources.NonEmptyCubeIndexBufferUAV;
	PrefixSumParameters.OutVertexIndexOffset = Resources.VertexIndexOffsetBufferUAV;

	FComputeShaderUtils::Dispatch(AsyncComputeCmdList, PrefixSumCSRef, PrefixSumParameters, DispatchSize);
	
	// Async compute UAV barriers
	AsyncComputeCmdList.Transition(FRHITransitionInfo{Resources.NonEmptyCubeLinearIdBufferUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute});
	AsyncComputeCmdList.Transition(FRHITransitionInfo{Resources.NonEmptyCubeIndexBufferUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute});
	AsyncComputeCmdList.Transition(FRHITransitionInfo{Resources.VertexIndexOffsetBufferUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute});
	AsyncComputeCmdList.Transition(FRHITransitionInfo{CounterBufferUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute});

	// Handle memory optimization mode - this still requires synchronization but now on async compute
	if (Parent && Parent->GetGenerationMode() == EVoxelMeshGenerationMode::MemoryOptimized)
	{
		// Create a fence for async compute
		if (!AsyncComputeFence)
		{
			AsyncComputeFence = RHICreateGPUFence(TEXT("VoxelMeshAsyncComputeFence"));
		}
		AsyncComputeFence->Clear();
		AsyncComputeCmdList.WriteGPUFence(AsyncComputeFence);
		
		// Submit async compute work and wait for completion
		AsyncComputeCmdList.SubmitCommandsHint();
		AsyncComputeFence->Wait();
		
		// Read counter buffer from main render thread
		ENQUEUE_RENDER_COMMAND(VoxelMeshReadCounter)([this, CounterBuffer, Resources, UniformParametersBuffer, GridBufferSRV, CubeIndexOffsetBufferSRV](FRHICommandListImmediate& RHICmdList) mutable
		{
			uint32 NumNonEmptyCubes = *(static_cast<uint32*>(RHICmdList.LockBuffer(CounterBuffer, 0, sizeof(uint32), RLM_ReadOnly)));
			RHICmdList.UnlockBuffer(CounterBuffer);
			
			// Resize buffers based on actual count
			uint32 RequiredVertexCount = NumNonEmptyCubes * 12;
			uint32 RequiredIndexCount = NumNonEmptyCubes * 15 * 3;
			ResizeBuffer_RenderThread(RequiredVertexCount * sizeof(FVector4f), RequiredIndexCount * sizeof(uint32));
			
			// Continue with async compute for final mesh generation
			FRHIAsyncComputeCommandListImmediate& FinalAsyncComputeCmdList = FRHICommandListExecutor::GetImmediateAsyncComputeCommandList();
			
			// Clear counter for final pass
			FUnorderedAccessViewRHIRef CounterBufferUAV = FinalAsyncComputeCmdList.CreateUnorderedAccessView(CounterBuffer, 
				FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(PF_R32_UINT));
			FinalAsyncComputeCmdList.ClearUAVUint(CounterBufferUAV, FUintVector4(0, 0, 0, 0));
			
			// Continue with mesh generation
			FinalizeMeshGenerationAsync(FinalAsyncComputeCmdList, NumNonEmptyCubes, Resources, UniformParametersBuffer, GridBufferSRV, CubeIndexOffsetBufferSRV);
		});
	}
	else
	{
		// Performance optimized mode - allocate max buffers and continue immediately
		uint32 EstimatedMaxVertices = static_cast<uint32>(TotalCubes) * 12;
		uint32 EstimatedMaxIndices = static_cast<uint32>(TotalCubes) * 15 * 3;
		
		// Resize buffers on render thread then continue async
		ENQUEUE_RENDER_COMMAND(VoxelMeshResizeBuffers)([this, EstimatedMaxVertices, EstimatedMaxIndices, Resources, UniformParametersBuffer, GridBufferSRV, CubeIndexOffsetBufferSRV](FRHICommandListImmediate& RHICmdList)
		{
			ResizeBuffer_RenderThread(EstimatedMaxVertices * sizeof(FVector4f), EstimatedMaxIndices * sizeof(uint32));
			
			// Continue with async compute for final mesh generation
			FRHIAsyncComputeCommandListImmediate& FinalAsyncComputeCmdList = FRHICommandListExecutor::GetImmediateAsyncComputeCommandList();
			FinalizeMeshGenerationAsync(FinalAsyncComputeCmdList, Resources.EstimatedNonEmptyCubes, Resources, UniformParametersBuffer, GridBufferSRV, CubeIndexOffsetBufferSRV);
		});
	}

	// End RenderDoc Capture
	if (bShouldCaptureRenderDoc)
	{
		IRenderCaptureProvider::Get().EndCapture(&AsyncComputeCmdList);
	}
}

void FVoxelChunkViewRHIProxy::FinalizeMeshGenerationAsync(FRHIAsyncComputeCommandListImmediate& AsyncComputeCmdList, uint32 NumNonEmptyCubes, 
	const FVoxelProcessingResources& Resources, 
	TUniformBufferRef<FVoxelMarchingCubeUniformParameters> UniformParametersBuffer,
	FShaderResourceViewRHIRef GridBufferSRV,
	FShaderResourceViewRHIRef CubeIndexOffsetBufferSRV)
{
	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	if (!ShaderMap)
	{
		UE_LOG(LogVoxelMesh, Error, TEXT("Cannot get global shader map in FinalizeMeshGenerationAsync"));
		bIsReady.store(true, std::memory_order_release);
		bIsAsyncGenerating.store(false, std::memory_order_release);
		return;
	}

	// Step 3: Generate Mesh (async)
	FVoxelMarchingCubesGenerateMeshCS::FParameters GenerateMeshParameter;
	GenerateMeshParameter.NumNonEmptyCubes = NumNonEmptyCubes;
	GenerateMeshParameter.InNonEmptyCubeIndex = Resources.NonEmptyCubeIndexBufferSRV;
	GenerateMeshParameter.InNonEmptyCubeLinearId = Resources.NonEmptyCubeLinearIdBufferSRV;
	GenerateMeshParameter.InVertexIndexOffset = Resources.VertexIndexOffsetBufferSRV;
	GenerateMeshParameter.OutVertexBuffer = MeshVertexBufferUAV;
	GenerateMeshParameter.OutIndexBuffer = MeshIndexBufferUAV;
	GenerateMeshParameter.MarchingCubeParameters = UniformParametersBuffer;
	GenerateMeshParameter.SrcVoxelData = GridBufferSRV;
	GenerateMeshParameter.InCubeIndexOffsets = CubeIndexOffsetBufferSRV;
	
	auto GenerateMeshCSRef = ShaderMap->GetShader<FVoxelMarchingCubesGenerateMeshCS>();
	
	// Calculate dispatch size based on non-empty cubes
	const FIntVector MeshDispatchSize = GetDispatchSize(NumNonEmptyCubes);
	FComputeShaderUtils::Dispatch(AsyncComputeCmdList, GenerateMeshCSRef, GenerateMeshParameter, MeshDispatchSize);

	// Create completion fence for async compute
	if (!AsyncComputeFence)
	{
		AsyncComputeFence = RHICreateGPUFence(TEXT("VoxelMeshAsyncComputeCompletionFence"));
	}
	AsyncComputeFence->Clear();
	AsyncComputeCmdList.WriteGPUFence(AsyncComputeFence);
	
	// Submit the final async compute work
	AsyncComputeCmdList.SubmitCommandsHint();
	
	// Schedule completion notification on render thread
	ENQUEUE_RENDER_COMMAND(VoxelMeshAsyncComplete)([this](FRHICommandListImmediate& RHICmdList)
	{
		// Wait for async compute to finish
		if (AsyncComputeFence)
		{
			AsyncComputeFence->Wait();
		}
		
		// Verify that mesh buffers are valid
		if (!MeshVertexBuffer || !MeshIndexBuffer)
		{
			UE_LOG(LogVoxelMesh, Error, TEXT("Async mesh generation completed but buffers are invalid"));
			bIsReady.store(false, std::memory_order_release);
			bIsAsyncGenerating.store(false, std::memory_order_release);
			return;
		}
		
		// Notify that mesh is ready
		if (const UVoxelChunkView* VoxelChunkView = Parent.Get())
		{
			VoxelChunkView->OnBuildFinished.Broadcast();
		}
		
		// Mark as ready and not generating
		bIsReady.store(true, std::memory_order_release);
		bIsAsyncGenerating.store(false, std::memory_order_release);
		
		// Log performance information
		const double GenerationTime = FPlatformTime::Seconds() - AsyncStartTime;
		UE_LOG(LogVoxelMesh, Log, TEXT("Async voxel mesh generation completed in %.2f ms"), GenerationTime * 1000.0);
	});
}
