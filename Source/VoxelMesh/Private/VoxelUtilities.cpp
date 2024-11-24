// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelUtilities.h"
#include "VoxelChunkView.h"
#include "VoxelVdbCommon.h"

UVoxelChunkView* UVoxelUtilities::CreateSphereChunkView(UObject* Outer)
{
	nanovdb::GridHandle<nanovdb::HostBuffer> NewGrid = nanovdb::tools::createLevelSetSphere<nanovdb::Fp4, nanovdb::HostBuffer>(100.f, nanovdb::Vec3f(100.f), 5.0f);
	UVoxelChunkView* ChunkView = NewObject<UVoxelChunkView>(Outer);
	ChunkView->SetVdbBuffer_GameThread(MoveTemp(NewGrid));
	return ChunkView;
}
