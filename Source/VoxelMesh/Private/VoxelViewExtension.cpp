#include "VoxelViewExtension.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "VoxelChunkView.h"
#include "VoxelShaders.h"
#include "UObject/GCObjectScopeGuard.h"

void FVoxelViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
}

void FVoxelViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
}

void FVoxelViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
}

void FVoxelViewExtension::PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
}

void FVoxelViewExtension::PreRenderBasePass_RenderThread(FRDGBuilder& GraphBuilder, bool bDepthBufferIsPopulated)
{
	const auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FVoxelMarchingCubesCS>();
	for (UVoxelChunkView* Chunk : Chunks)
	{
		if (IsValid(Chunk))
		{
		}
	}
}
