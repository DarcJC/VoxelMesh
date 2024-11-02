// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VoxelRHIUtility.h"
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

public:
	UPROPERTY(EditAnywhere, Category = "Voxel")
	uint32 DimensionX = 128;
	
	UPROPERTY(EditAnywhere, Category = "Voxel")
	uint32 DimensionY = 128;
	
	UPROPERTY(EditAnywhere, Category = "Voxel")
	uint32 DimensionZ = 128;

private:
	bool bRequireRebuild = false;

	friend class UVoxelRenderingWorldSubsystem;
};
