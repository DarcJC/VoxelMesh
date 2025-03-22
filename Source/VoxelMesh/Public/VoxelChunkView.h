// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
#include "AssetTypeActions_Base.h"
#endif // WITH_EDITOR

#include "UObject/Object.h"
#include "VoxelRHIUtility.h"
#include "VoxelVdbCommon.h"
#include "VoxelChunkView.generated.h"

class FVoxelMarchingCubesUniforms;
struct FVoxelChunkViewRHIProxy;

DECLARE_MULTICAST_DELEGATE(FVoxelChunkMeshBuildFinishedDelegate);

UCLASS(BlueprintType, EditInlineNew)
class VOXELMESH_API UVoxelChunkView : public UObject
{
	GENERATED_BODY()

	using FVoxelElementType = int32;

public:
	UVoxelChunkView(const FObjectInitializer& ObjectInitializer);
	virtual ~UVoxelChunkView() override;
	
	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsDirty() const;

	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsEmpty() const;

	UFUNCTION(BlueprintCallable)
	void RebuildMesh();

	TSharedPtr<FVoxelChunkViewRHIProxy> GetRHIProxy();

	void MarkAsDirty();

	void SetVdbBuffer_GameThread(nanovdb::GridHandle<nanovdb::HostBuffer>&& NewBuffer);

	UFUNCTION(BlueprintSetter)
	void UpdateSurfaceIsoValue(float NewValue);

	virtual void Serialize(FArchive& Ar) override;

	FVoxelChunkMeshBuildFinishedDelegate OnBuildFinished;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	
	UPROPERTY(EditAnywhere, BlueprintSetter=UpdateSurfaceIsoValue, Category = "Voxel")
	float SurfaceIsoValue = 0.0f;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Voxel | Debug")
	uint32 DimensionX;
	
	UPROPERTY(VisibleAnywhere, Category = "Voxel | Debug")
	uint32 DimensionY;
	
	UPROPERTY(VisibleAnywhere, Category = "Voxel | Debug")
	uint32 DimensionZ;

	nanovdb::GridHandle<nanovdb::HostBuffer> HostVdbBuffer;

	UPROPERTY()
	TArray<uint8> VdbBulkData;

private:
	TSharedPtr<FVoxelChunkViewRHIProxy> RHIProxy;
	
	friend class UVoxelRenderingWorldSubsystem;
	friend struct FVoxelChunkViewRHIProxy;
};

struct FVoxelChunkViewRHIProxy
{
	explicit FVoxelChunkViewRHIProxy(const UVoxelChunkView* ChunkView);

	void ResizeBuffer_RenderThread(uint32_t NewVBSize, uint32 NewIBSize);
	void RegenerateMesh_RenderThread(FRHICommandListImmediate& RHICmdList);
	void RegenerateMesh_GameThread();
	void RegenerateMesh();

	bool IsReady() const;
	bool IsGenerating() const;

	TObjectPtr<UVoxelChunkView> Parent;
	TRefCountPtr<FRHIBuffer> MeshVertexBuffer;
	TRefCountPtr<FRHIBuffer> MeshIndexBuffer;
	TRefCountPtr<FRHIUnorderedAccessView> MeshVertexBufferUAV;
	TRefCountPtr<FRHIUnorderedAccessView> MeshIndexBufferUAV;
	TArray<uint8> VoxelDataBuffer;
	
	// 替换单一的VoxelSize为三个独立的维度
	uint32 VoxelSizeX;
	uint32 VoxelSizeY;
	uint32 VoxelSizeZ;
	
	float SurfaceIsoValue = 0.0f;
	std::atomic<bool> bIsReady;
};

