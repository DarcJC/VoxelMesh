// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VoxelRHIUtility.h"
#include "VoxelVdbCommon.h"
#include "VoxelChunkView.generated.h"

class FVoxelMarchingCubesUniforms;
struct FVoxelChunkViewRHIProxy;

UCLASS(BlueprintType)
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
	void TestDispatch();

	void RebuildMesh();

	TSharedPtr<FVoxelChunkViewRHIProxy> GetRHIProxy();

	void MarkAsDirty();

	void SetVdbBuffer_GameThread(nanovdb::GridHandle<nanovdb::HostBuffer>&& NewBuffer);

	virtual void Serialize(FArchive& Ar) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Voxel | Debug")
	uint32 DimensionX;
	
	UPROPERTY(VisibleAnywhere, Category = "Voxel | Debug")
	uint32 DimensionY;
	
	UPROPERTY(VisibleAnywhere, Category = "Voxel | Debug")
	uint32 DimensionZ;

	nanovdb::GridHandle<nanovdb::HostBuffer> HostVdbBuffer;

private:
	TSharedPtr<FVoxelChunkViewRHIProxy> RHIProxy;
	
	friend class UVoxelRenderingWorldSubsystem;
	friend struct FVoxelChunkViewRHIProxy;
};

struct FVoxelChunkViewRHIProxy
{
	explicit FVoxelChunkViewRHIProxy(UVoxelChunkView* ChunkView);

	void ResizeBuffer_RenderThread(uint32_t NewVBSize, uint32 NewIBSize);
	void RegenerateMesh_RenderThread();
	void RegenerateMesh_GameThread();
	void RegenerateMesh();

	TRefCountPtr<FRHIBuffer> MeshVertexBuffer;
	TRefCountPtr<FRHIBuffer> MeshIndexBuffer;
	TRefCountPtr<FRHIUnorderedAccessView> MeshVertexBufferUAV;
	TRefCountPtr<FRHIUnorderedAccessView> MeshIndexBufferUAV;
	TArray<uint8> VoxelDataBuffer;
	uint32 VoxelSize;
	std::atomic<bool> bIsReady;
};
