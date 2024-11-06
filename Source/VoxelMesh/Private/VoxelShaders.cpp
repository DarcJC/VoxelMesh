#include "VoxelShaders.h"

IMPLEMENT_UNIFORM_BUFFER_STRUCT(FVoxelMarchingCubeUniformParameters, "MarchingCubeParameters")
IMPLEMENT_GLOBAL_SHADER(FVoxelMarchingCubesCalcCubeIndexCS, "/Plugin/VoxelMesh/MarchingCubesCS.usf", "CalcCubeIndexCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVoxelMarchingCubesCalcCubeOffsetCS, "/Plugin/VoxelMesh/MarchingCubesCS.usf", "CalcVertexAndIndexPrefixSumCS", SF_Compute);

void FVoxelMarchingCubesCalcCubeIndexCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
{
	Environment.SetDefine(TEXT("VOXEL_WRITABLE_CUBE_INDEX_OFFSETS"), true);
}
