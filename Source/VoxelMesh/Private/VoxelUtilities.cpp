// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelUtilities.h"
#define PNANOVDB_C
#define PNANOVDB_ADDRESS_64
#include "PNanoVDB.ush"
#undef PNANOVDB_ADDRESS_64
#undef PNANOVDB_C

UVoxelChunkView* UVoxelUtilities::CreateSphereChunkView()
{
	return nullptr;
}
