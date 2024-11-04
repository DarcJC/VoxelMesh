// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelUtilities.h"
#include "VoxelChunkView.h"
#include "VoxelVdbCommon.h"

UVoxelChunkView* UVoxelUtilities::CreateSphereChunkView(UObject* Outer)
{
	nanovdb::GridHandle<nanovdb::HostBuffer> NewGrid = nanovdb::tools::createLevelSetSphere();
	UVoxelChunkView* ChunkView = NewObject<UVoxelChunkView>(Outer);
	ChunkView->SetVdbBuffer_GameThread(MoveTemp(NewGrid));
	return ChunkView;
}
