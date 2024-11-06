// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelChunkView.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "VoxelRenderingWorldSubsystem.h"
#include "VoxelShaders.h"

#include "IRenderCaptureProvider.h"
#include "Engine/TextureRenderTarget2D.h"

UVoxelChunkView::UVoxelChunkView(const FObjectInitializer& ObjectInitializer)
{
	DimensionX = 1;
	DimensionY = 1;
	DimensionZ = 1;
	RenderTarget_Debug = ObjectInitializer.CreateDefaultSubobject<UTextureRenderTarget2D>(this, TEXT("DebugRenderTarget"));
}

UVoxelChunkView::~UVoxelChunkView()
{
}

bool UVoxelChunkView::IsDirty() const
{
	return bRequireRebuild;
}

void UVoxelChunkView::MarkAsDirty()
{
	bRequireRebuild = true;
}

void UVoxelChunkView::SetVdbBuffer_GameThread(nanovdb::GridHandle<nanovdb::HostBuffer>&& NewBuffer)
{
	MarkAsDirty();
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
}

void UVoxelChunkView::Serialize(FArchive& Ar)
{
	UObject::Serialize(Ar);

	if (Ar.IsLoading())
	{
		MarkAsDirty();
	}

	FBulkDataBuffer<uint8> Buffer;
}

void UVoxelChunkView::TestDispatch()
{
	ENQUEUE_RENDER_COMMAND(QwQVoxel)([this] (FRHICommandListImmediate& CmdList)
	{
		FRDGBuilder GraphBuilder(CmdList);
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		check(ShaderMap);

		const size_t TotalCubes = DimensionX * DimensionY * DimensionZ;

		// RenderDoc Capture
		FRDGPassRef BeginCapturePass = GraphBuilder.AddPass(
			RDG_EVENT_NAME("BeginCapture"),
			ERDGPassFlags::None,
			[] (FRHICommandListImmediate& RHICommandListLocal)
		{
			IRenderCaptureProvider::Get().BeginCapture(&RHICommandListLocal, IRenderCaptureProvider::ECaptureFlags_Launch);
		});
		
		// Atomic counter buffer
		static constexpr uint32 DEFAULT_COUNTER_VALUES[] { 0, 0, 0, 0 };
		FRDGBufferDesc Desc = FRDGBufferDesc::CreateUploadDesc(sizeof(uint32), 4);
		Desc.Usage |= EBufferUsageFlags::UnorderedAccess;
		FRDGBufferRef CounterBuffer = GraphBuilder.CreateBuffer(Desc, TEXT("Voxel Atomic Counter"));
		GraphBuilder.QueueBufferUpload(CounterBuffer, DEFAULT_COUNTER_VALUES, std::size(DEFAULT_COUNTER_VALUES));
		FRDGBufferUAVRef CounterBufferUAV = GraphBuilder.CreateUAV(CounterBuffer, EPixelFormat::PF_R32_UINT);

		// Uniform buffer
		FVoxelMarchingCubeUniformParameters* UniformParameters =GraphBuilder.AllocParameters<FVoxelMarchingCubeUniformParameters>();
		UniformParameters->VoxelSize = DimensionX;
		UniformParameters->SurfaceIsoValue = 0.f;
		UniformParameters->TotalCubes = (DimensionX + 1) * (DimensionY + 1) * (DimensionZ + 1);
		TRDGUniformBufferRef<FVoxelMarchingCubeUniformParameters> UniformParametersBuffer = GraphBuilder.CreateUniformBuffer(UniformParameters);

		// Nanovdb data buffer
		nanovdb::NanoGrid<float>* GridData = HostVdbBuffer.grid<float>();
		FRDGBufferDesc GridBufferDesc = FRDGBufferDesc::CreateUploadDesc(sizeof(float), HostVdbBuffer.size());
		FRDGBufferRef GridBuffer = GraphBuilder.CreateBuffer(GridBufferDesc, TEXT("Voxel Data Buffer"));
		GraphBuilder.QueueBufferUpload(GridBuffer, GridData, HostVdbBuffer.size());
		FRDGBufferSRVRef GridBufferSRV = GraphBuilder.CreateSRV(GridBuffer, EPixelFormat::PF_R32_UINT);

		// Cube index offset buffer
		FRDGBufferDesc CubeIndexOffsetBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TotalCubes);
		FRDGBufferRef CubeIndexOffsetBuffer = GraphBuilder.CreateBuffer(CubeIndexOffsetBufferDesc, TEXT("Cube Index Offset"));
		FRDGBufferUAVRef CubeIndexOffsetBufferUAV = GraphBuilder.CreateUAV(CubeIndexOffsetBuffer, EPixelFormat::PF_R32_UINT);
		FRDGBufferSRVRef CubeIndexOffsetBufferSRV = GraphBuilder.CreateSRV(CubeIndexOffsetBuffer, EPixelFormat::PF_R32_UINT);

		// We create these buffer later
		FRDGBufferRef NonEmptyCubeLinearIdBuffer = nullptr;
		FRDGBufferUAVRef NonEmptyCubeLinearIdBufferUAV = nullptr;
		FRDGBufferSRVRef NonEmptyCubeLinearIdBufferSRV = nullptr;
		
		FRDGBufferRef NonEmptyCubeIndexBuffer = nullptr;
		FRDGBufferUAVRef NonEmptyCubeIndexBufferUAV = nullptr;
		FRDGBufferSRVRef NonEmptyCubeIndexBufferSRV = nullptr;
		
		FRDGBufferRef VertexIndexOffsetBuffer = nullptr;
		FRDGBufferUAVRef VertexIndexOffsetBufferUAV = nullptr;
		FRDGBufferSRVRef VertexIndexOffsetBufferSRV = nullptr;
		
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
		/// Read back num non empty cube from counter
		/////////////////////////
		TSharedRef<FRHIGPUBufferReadback> CounterReadbackRequest = MakeShared<FRHIGPUBufferReadback>(TEXT("Counter Readback Request"));
		auto* ReadbackPassParameters = GraphBuilder.AllocParameters<FReadbackBufferParameters>();
		ReadbackPassParameters->Buffer = CounterBuffer;
		GraphBuilder.AddPass(RDG_EVENT_NAME("Readback counter"), ReadbackPassParameters, ERDGPassFlags::Readback, [CounterReadbackRequest, CounterBuffer,
				&NonEmptyCubeLinearIdBuffer, &NonEmptyCubeLinearIdBufferUAV, &NonEmptyCubeLinearIdBufferSRV,
				&NonEmptyCubeIndexBuffer, &NonEmptyCubeIndexBufferUAV, &NonEmptyCubeIndexBufferSRV,
				&VertexIndexOffsetBuffer, &VertexIndexOffsetBufferUAV, &VertexIndexOffsetBufferSRV] (FRHICommandListImmediate& RHICommandListLocal)
		{
			CounterReadbackRequest->EnqueueCopy(RHICommandListLocal, CounterBuffer->GetRHI(), 0U);
			CounterReadbackRequest->Wait(RHICommandListLocal, FRHIGPUMask::All());
			uint32 NumNonEmptyCubes = *(static_cast<uint32*>(CounterReadbackRequest->Lock(3 * sizeof(uint32))));
			CounterReadbackRequest->Unlock();
		});

		/////////////////////////
		/// Prefix sum
		/////////////////////////
		auto PrefixSumCSRef = ShaderMap->GetShader<FVoxelMarchingCubesCalcCubeOffsetCS>();
		auto* PrefixSumParameters = GraphBuilder.AllocParameters<FVoxelMarchingCubesCalcCubeOffsetCS::FParameters>();
		
		PrefixSumParameters->Counter = CounterBufferUAV;
		PrefixSumParameters->MarchingCubeParameters = UniformParametersBuffer;
		PrefixSumParameters->SrcVoxelData = GridBufferSRV;
		PrefixSumParameters->InCubeIndexOffsets = CubeIndexOffsetBufferSRV;
		PrefixSumParameters->OutNonEmptyCubeLinearId = NonEmptyCubeLinearIdBufferUAV;
		PrefixSumParameters->OutNonEmptyCubeIndex = NonEmptyCubeIndexBufferUAV;
		PrefixSumParameters->OutVertexIndexOffset = VertexIndexOffsetBufferUAV;
		
		FRDGPassRef PrefixSumPass = FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Voxel Marching Cubes Prefix Sum"), PrefixSumCSRef, PrefixSumParameters, GetDispatchSize(TotalCubes));
		
		// End RenderDoc Capture
		FRDGPassRef EndCapturePass = GraphBuilder.AddPass(RDG_EVENT_NAME("End Capture"), ERDGPassFlags::None, [] (FRHICommandListImmediate& RHICmdList) 
		{
			IRenderCaptureProvider::Get().EndCapture(&RHICmdList);
		});

		GraphBuilder.AddPassDependency(BeginCapturePass, CalcPass);
		GraphBuilder.AddPassDependency(CalcPass, PrefixSumPass);
		GraphBuilder.AddPassDependency(PrefixSumPass, EndCapturePass);

		GraphBuilder.Execute();
	});
}
