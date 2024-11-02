// Copyright Epic Games, Inc. All Rights Reserved.

#include "VoxelMesh.h"

#include "VoxelShaders.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FVoxelMeshModule"

void FVoxelMeshModule::StartupModule()
{
	const FString ShaderDir = FPaths::Combine(
		IPluginManager::Get().FindPlugin(TEXT("VoxelMesh"))->GetBaseDir(),
		TEXT("Shaders")
	);

	AddShaderSourceDirectoryMapping(TEXT("/Plugin/VoxelMesh"), ShaderDir);
}

void FVoxelMeshModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FVoxelMeshModule, VoxelMesh)

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FVoxelMarchingCubesUniforms, "FieldInfo")
IMPLEMENT_GLOBAL_SHADER(FVoxelMarchingCubesCS, "/Plugin/VoxelMesh/MarchingCubesCS.usf", "MainCS", SF_Compute);
