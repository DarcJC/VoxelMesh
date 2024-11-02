// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "VoxelMeshComponent.generated.h"


class UVoxelChunkView;

UCLASS(BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class VOXELMESH_API UVoxelMeshComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UVoxelMeshComponent(const FObjectInitializer& ObjectInitializer);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Voxel)
	UVoxelChunkView* ChunkView;
};
