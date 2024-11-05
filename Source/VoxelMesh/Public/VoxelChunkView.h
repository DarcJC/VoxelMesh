// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VoxelRHIUtility.h"
#include "VoxelVdbCommon.h"
#include "VoxelChunkView.generated.h"

class FVoxelMarchingCubesUniforms;

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

	void MarkAsDirty();

	void SetVdbBuffer_GameThread(nanovdb::GridHandle<nanovdb::HostBuffer>&& NewBuffer);

	virtual void Serialize(FArchive& Ar) override;

	UFUNCTION(BlueprintCallable)
	void TestDispatch();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Voxel | Property")
	uint32 DimensionX;
	
	UPROPERTY(VisibleAnywhere, Category = "Voxel | Property")
	uint32 DimensionY;
	
	UPROPERTY(VisibleAnywhere, Category = "Voxel | Property")
	uint32 DimensionZ;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Voxel | Debug")
	UTextureRenderTarget2D* RenderTarget_Debug;

	nanovdb::GridHandle<nanovdb::HostBuffer> HostVdbBuffer;

private:
	bool bRequireRebuild = false;

	friend class UVoxelRenderingWorldSubsystem;
};
