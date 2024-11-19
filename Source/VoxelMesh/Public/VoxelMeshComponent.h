// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "VoxelMeshComponent.generated.h"


class UVoxelChunkView;
struct FVoxelChunkPrimitiveSceneProxy;

/**
 * Primitive component to render generated voxel mesh
 */
UCLASS(BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class VOXELMESH_API UVoxelMeshProxyComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	// Begin USceneComponent interface
	virtual bool ShouldCreateRenderState() const override;
	virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	// End USceneComponent interface

	// Begin UPrimitiveComponent interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	// End UPrimitiveComponent interface

	UFUNCTION(BlueprintCallable)
	void UpdateChunkViewAsset(UVoxelChunkView* InAsset);

	void OnVoxelMeshReady();

protected:
	UPROPERTY(EditAnywhere, BlueprintSetter=UpdateChunkViewAsset, Category=Voxel, meta = (ExposeOnSpawn))
	UVoxelChunkView* ChunkViewAsset = nullptr;

	friend struct FVoxelChunkPrimitiveSceneProxy;
};

/**
 * Vertex factory to configure how to read voxel vertex buffer
 */
class FVoxelMeshVertexFactory : public FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE_API(FVoxelMeshVertexFactory, VOXELMESH_API)
public:
	FVoxelMeshVertexFactory(ERHIFeatureLevel::Type InFeatureLevel);

	virtual ~FVoxelMeshVertexFactory() override;

	struct FDataType
	{
		FVertexStreamComponent PositionStream;
		FVertexStreamComponent NormalStream;
	};

	/**
	* Should we cache the material's shadertype on this platform with this vertex factory?
	*/
	static VOXELMESH_API bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	/**
	* Can be overridden by FVertexFactory subclasses to modify their compile environment just before compilation occurs.
	*/
	static VOXELMESH_API void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
	
	/**
	* Get vertex elements used when during PSO precaching materials using this vertex factory type
	*/
	static VOXELMESH_API void GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType, FVertexDeclarationElementList& Elements);
	
	// Begin FRenderResource interface.
	VOXELMESH_API virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseResource() override final { FVertexFactory::ReleaseResource(); }
	// End FRenderResource interface.

	FDataType Data;
};

/**
 * Scene proxy of UVoxelMeshProxyComponent
 */
struct VOXELMESH_API FVoxelChunkPrimitiveSceneProxy final : public FPrimitiveSceneProxy
{
	FVoxelChunkPrimitiveSceneProxy(UVoxelMeshProxyComponent* VoxelMeshProxyComponent);

	// Begin FPrimitiveSceneProxy interface
	virtual SIZE_T GetTypeHash() const override;
	virtual uint32 GetMemoryFootprint() const override;
	virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;
	virtual void DestroyRenderThreadResources() override;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, class FMeshElementCollector& Collector) const override;
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	// End FPrimitiveSceneProxy interface

	void TryInitialize() const;

	mutable  FVertexBuffer VertexBuffer;
	mutable  FIndexBuffer IndexBuffer;
	mutable  FVoxelMeshVertexFactory VertexFactory;
	mutable  uint32 NumPrimitives;
	mutable uint32 NumVertices;
	mutable UVoxelMeshProxyComponent* VoxelMeshProxyComponent;
	mutable  bool bIsInitialized;
};

class FVoxelMeshVertexFactoryVertexShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FVoxelMeshVertexFactoryVertexShaderParameters, NonVirtual);
public:	
	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* InView,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const;
};

class FVoxelMeshVertexFactoryPixelShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FVoxelMeshVertexFactoryPixelShaderParameters, NonVirtual);
public:	
	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* InView,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const;
};


