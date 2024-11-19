// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelMeshComponent.h"

#include "MaterialDomain.h"
#include "MeshMaterialShader.h"
#include "VoxelChunkView.h"
#include "Materials/MaterialRenderProxy.h"


bool UVoxelMeshProxyComponent::ShouldCreateRenderState() const
{
	const bool bIsCPUResourceValid = IsValid(ChunkViewAsset) && !ChunkViewAsset->IsEmpty();
	return bIsCPUResourceValid;
}

void UVoxelMeshProxyComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);

	check(IsValid(ChunkViewAsset));
	if (!ChunkViewAsset->GetRHIProxy().IsValid() || !ChunkViewAsset->GetRHIProxy()->IsReady())
	{
		if (ChunkViewAsset->GetRHIProxy() && !ChunkViewAsset->GetRHIProxy()->IsGenerating())
		{
			ChunkViewAsset->RebuildMesh();
		}
	}
}

FBoxSphereBounds UVoxelMeshProxyComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	const FBoxSphereBounds StaticBounds(FVector(0, 0, 0), FVector(30.f, 30.f, 30.f), 30.f);
	return StaticBounds.TransformBy(LocalToWorld);
}

FPrimitiveSceneProxy* UVoxelMeshProxyComponent::CreateSceneProxy()
{
	return new FVoxelChunkPrimitiveSceneProxy(this);
}

void UVoxelMeshProxyComponent::UpdateChunkViewAsset(UVoxelChunkView* InAsset)
{
	check(InAsset == nullptr || IsValid(InAsset));
	if (UVoxelChunkView* OldView = ChunkViewAsset; IsValid(OldView))
	{
		OldView->OnBuildFinished.RemoveAll(this);
	}
	ChunkViewAsset = InAsset;
	if (IsValid(ChunkViewAsset))
	{
		ChunkViewAsset->OnBuildFinished.AddUObject(this, &UVoxelMeshProxyComponent::OnVoxelMeshReady);
	}
	MarkRenderStateDirty();
}

void UVoxelMeshProxyComponent::OnVoxelMeshReady()
{
	if (IsInGameThread())
	{
		MarkRenderStateDirty();
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			MarkRenderStateDirty();
		});
	}
}

FVoxelChunkPrimitiveSceneProxy::FVoxelChunkPrimitiveSceneProxy(UVoxelMeshProxyComponent* InVoxelMeshProxyComponent)
	: FPrimitiveSceneProxy(InVoxelMeshProxyComponent)
	, VertexFactory(GMaxRHIFeatureLevel)
	, VoxelMeshProxyComponent(InVoxelMeshProxyComponent)
	, bIsInitialized(false)
{
	check(IsValid(InVoxelMeshProxyComponent->ChunkViewAsset));
	TryInitialize();
}

SIZE_T FVoxelChunkPrimitiveSceneProxy::GetTypeHash() const
{
	static uint8 Hash;
	return reinterpret_cast<size_t>(&Hash);
}

uint32 FVoxelChunkPrimitiveSceneProxy::GetMemoryFootprint() const
{
	return sizeof(*this) + GetAllocatedSize();
}

void FVoxelChunkPrimitiveSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
	TryInitialize();
	if (bIsInitialized)
	{
		VertexFactory.Data.PositionStream = FVertexStreamComponent(&VertexBuffer, 0, sizeof(FVector4f), VET_Float3);
		VertexFactory.Data.NormalStream = FVertexStreamComponent(&VertexBuffer, sizeof(FVector3f), sizeof(uint32), VET_UInt);
		
		VertexFactory.InitResource(RHICmdList);
		VertexBuffer.InitResource(RHICmdList);
		IndexBuffer.InitResource(RHICmdList);
	}
}

void FVoxelChunkPrimitiveSceneProxy::DestroyRenderThreadResources()
{
	VertexFactory.ReleaseResource();
	VertexBuffer.SetRHI(nullptr);
	VertexBuffer.ReleaseResource();
	IndexBuffer.SetRHI(nullptr);
	IndexBuffer.ReleaseResource();
}

void FVoxelChunkPrimitiveSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
                                                            const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, class FMeshElementCollector& Collector) const
{
	if (!bIsInitialized)
	{
		return;
	}
	
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FVoxelChunkPrimitiveSceneProxy_GetMeshElements);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];
			const FSceneViewFamily& ViewSpecificFamily = *View->Family;
			const bool bIsWireframe = ViewSpecificFamily.EngineShowFlags.Wireframe;

			FMeshBatch& MeshBatch = Collector.AllocateMesh();
			
			// FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

			FMaterialRenderProxy* MaterialProxy = nullptr;

			// Set up wire frame material (if needed)
			const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;
			{
				// if (bWireframe)
				{
					const FEngineShowFlags& EngineShowFlags = ViewFamily.EngineShowFlags;
					const bool bActorColorationEnabled = EngineShowFlags.ActorColoration;

					const FLinearColor WireColor = GetWireframeColor();
					const FLinearColor ViewWireframeColor(bActorColorationEnabled ? GetPrimitiveColor() : WireColor);

					FColoredMaterialRenderProxy* WireframeMaterialInstance = new FColoredMaterialRenderProxy(
						GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
						GetSelectionColor(ViewWireframeColor, !(GIsEditor && EngineShowFlags.Selection) || IsSelected(), IsHovered(),
										  false)
					);

					Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
					MaterialProxy = WireframeMaterialInstance;
				}
			}
			
			MeshBatch.MaterialRenderProxy = MaterialProxy;

			MeshBatch.bWireframe = true;
			MeshBatch.bSelectable = true;
			MeshBatch.VertexFactory = &VertexFactory;
			MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
			MeshBatch.CastShadow = true;
			MeshBatch.bUseForDepthPass = true;
			MeshBatch.bUseAsOccluder = ShouldUseAsOccluder() && GetScene().GetShadingPath() == EShadingPath::Deferred && !IsMovable();
			MeshBatch.bUseForMaterial = true;
			MeshBatch.Type = PT_TriangleList;
			MeshBatch.DepthPriorityGroup = SDPG_World;
			
			FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
			BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
			BatchElement.IndexBuffer = &IndexBuffer;
			BatchElement.NumPrimitives = NumPrimitives;
			BatchElement.FirstIndex = 0;
			BatchElement.MinVertexIndex = 0;
			BatchElement.MaxVertexIndex = NumVertices - 1;

			Collector.AddMesh(ViewIndex, MeshBatch);
			// PDI->DrawMesh(MeshBatch);
		}
	}
}

FPrimitiveViewRelevance FVoxelChunkPrimitiveSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	if (!bIsInitialized)
	{
		TryInitialize();
	}
	
	FPrimitiveViewRelevance ViewRelevance;
	ViewRelevance.bDrawRelevance = IsShown(View) && bIsInitialized;
	ViewRelevance.bDynamicRelevance = true;
	return ViewRelevance;
}

void FVoxelChunkPrimitiveSceneProxy::TryInitialize() const
{
	TSharedPtr<FVoxelChunkViewRHIProxy> RHIProxy = VoxelMeshProxyComponent->ChunkViewAsset->GetRHIProxy();
	if (RHIProxy->IsReady())
	{
		NumVertices = RHIProxy->MeshVertexBuffer->GetSize() / sizeof(FVector4f);
		NumPrimitives = RHIProxy->MeshIndexBuffer->GetSize() / sizeof(FUintVector3);
		VertexBuffer.SetRHI(RHIProxy->MeshVertexBuffer);
		IndexBuffer.SetRHI(RHIProxy->MeshIndexBuffer);
		bIsInitialized = true;
	}
	else
	{
		bIsInitialized = false;
	}
}

FVoxelMeshVertexFactory::FVoxelMeshVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
	: FVertexFactory(InFeatureLevel)
{
}

FVoxelMeshVertexFactory::~FVoxelMeshVertexFactory()
{
	ReleaseResource();
}

bool FVoxelMeshVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return (Parameters.MaterialParameters.MaterialDomain == EMaterialDomain::MD_Surface);
}

void FVoxelMeshVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters,
	FShaderCompilerEnvironment& OutEnvironment)
{
}

void FVoxelMeshVertexFactory::GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType,
	FVertexDeclarationElementList& Elements)
{
	Elements.Add(FVertexElement(0, 0, VET_Float3, 0, sizeof(FVector4f), false));
	Elements.Add(FVertexElement(1, sizeof(FVector3f), VET_UInt, 1, sizeof(FVector4f), false));
}

void FVoxelMeshVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	FVertexDeclarationElementList Elements;

	Elements.Add(AccessStreamComponent(Data.PositionStream, 0));
	Elements.Add(AccessStreamComponent(Data.NormalStream, 1));

	AddPrimitiveIdStreamElement(EVertexInputStreamType::Default, Elements, /* AttributeIndex = */ 1, /* AttributeIndex_Mobile = */0xFF);
	InitDeclaration(Elements);
}

void FVoxelMeshVertexFactoryVertexShaderParameters::GetElementShaderBindings(const class FSceneInterface* Scene,
	const FSceneView* InView, const class FMeshMaterialShader* Shader, const EVertexInputStreamType InputStreamType,
	ERHIFeatureLevel::Type FeatureLevel, const FVertexFactory* VertexFactory, const FMeshBatchElement& BatchElement,
	class FMeshDrawSingleShaderBindings& ShaderBindings, FVertexInputStreamArray& VertexStreams) const
{
}

void FVoxelMeshVertexFactoryPixelShaderParameters::GetElementShaderBindings(const class FSceneInterface* Scene,
	const FSceneView* InView, const class FMeshMaterialShader* Shader, const EVertexInputStreamType InputStreamType,
	ERHIFeatureLevel::Type FeatureLevel, const FVertexFactory* VertexFactory, const FMeshBatchElement& BatchElement,
	class FMeshDrawSingleShaderBindings& ShaderBindings, FVertexInputStreamArray& VertexStreams) const
{
}

IMPLEMENT_TYPE_LAYOUT(FVoxelMeshVertexFactoryVertexShaderParameters);
IMPLEMENT_TYPE_LAYOUT(FVoxelMeshVertexFactoryPixelShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FVoxelMeshVertexFactory, SF_Vertex, FVoxelMeshVertexFactoryVertexShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FVoxelMeshVertexFactory, SF_Pixel, FVoxelMeshVertexFactoryPixelShaderParameters);

IMPLEMENT_VERTEX_FACTORY_TYPE(FVoxelMeshVertexFactory, "/Plugin/VoxelMesh/VoxelMeshVertexFactory.ush",
                              EVertexFactoryFlags::UsedWithMaterials
                              | EVertexFactoryFlags::SupportsStaticLighting
                              | EVertexFactoryFlags::SupportsDynamicLighting
                              | EVertexFactoryFlags::SupportsCachingMeshDrawCommands
                              | EVertexFactoryFlags::SupportsPSOPrecaching
);
