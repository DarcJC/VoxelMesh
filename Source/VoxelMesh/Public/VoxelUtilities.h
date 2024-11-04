// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VoxelUtilities.generated.h"


class UVoxelChunkView;

/**
 * Programming interfaces and blueprint interfaces of some helper function for voxel.
 */
UCLASS()
class VOXELMESH_API UVoxelUtilities : public UObject
{
	GENERATED_BODY()

public:

	/// Create a test nanovdb buffer
	UFUNCTION(BlueprintCallable)
	static UVoxelChunkView* CreateSphereChunkView(UObject* Outer);
};
