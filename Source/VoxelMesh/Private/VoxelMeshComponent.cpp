// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelMeshComponent.h"


UVoxelMeshComponent::UVoxelMeshComponent(const FObjectInitializer& ObjectInitializer)
{
	ChunkView = ObjectInitializer.CreateDefaultSubobject<UVoxelChunkView>(this, TEXT("ChunkView"));
}
