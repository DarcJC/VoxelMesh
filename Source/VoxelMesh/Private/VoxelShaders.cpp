#include "VoxelShaders.h"

IMPLEMENT_GLOBAL_SHADER(FVoxelMarchingCubesCS, "/Plugin/VoxelMesh/MarchingCubesCS.usf", "MainCS", SF_Compute);
