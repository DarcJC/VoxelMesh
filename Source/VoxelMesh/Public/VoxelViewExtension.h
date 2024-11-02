#pragma once
#include "SceneViewExtension.h"

class VOXELMESH_API FVoxelViewExtension : public FWorldSceneViewExtension
{
public:
	FVoxelViewExtension(const FAutoRegister& AutoReg, UWorld* InWorld) : FWorldSceneViewExtension(AutoReg, InWorld) {}

	// Begin ISceneViewExtension interface
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override;
	virtual void PreRenderBasePass_RenderThread(FRDGBuilder& GraphBuilder, bool bDepthBufferIsPopulated) override;
	// End ISceneViewExtension interface

private:
	TArrayView<class UVoxelChunkView*> Chunks;
	
	friend class UVoxelRenderingWorldSubsystem;
};
