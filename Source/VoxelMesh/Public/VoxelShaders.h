#pragma once

#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

#if 1
#define VOXEL_SHADER_PARAMETER_BUFFER_SRV(...) SHADER_PARAMETER_SRV(__VA_ARGS__)
#define VOXEL_SHADER_PARAMETER_BUFFER_UAV(...) SHADER_PARAMETER_UAV(__VA_ARGS__)
#else
#define VOXEL_SHADER_PARAMETER_BUFFER_SRV(...) SHADER_PARAMETER_RDG_BUFFER_SRV(__VA_ARGS__)
#define VOXEL_SHADER_PARAMETER_BUFFER_UAV(...) SHADER_PARAMETER_RDG_BUFFER_UAV(__VA_ARGS__)
#endif

BEGIN_UNIFORM_BUFFER_STRUCT(FVoxelMarchingCubeUniformParameters, VOXELMESH_API )
	SHADER_PARAMETER(uint32, VoxelSize)
	SHADER_PARAMETER(uint32, TotalCubes)
	SHADER_PARAMETER(float, SurfaceIsoValue)
END_UNIFORM_BUFFER_STRUCT()

class VOXELMESH_API FVoxelMarchingCubesCalcCubeIndexCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelMarchingCubesCalcCubeIndexCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelMarchingCubesCalcCubeIndexCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FVoxelMarchingCubeUniformParameters, MarchingCubeParameters)
		VOXEL_SHADER_PARAMETER_BUFFER_SRV(StructuredBuffer<uint32>, SrcVoxelData)
		VOXEL_SHADER_PARAMETER_BUFFER_UAV(RWBuffer<uint32>, OutCubeIndexOffsets)
		VOXEL_SHADER_PARAMETER_BUFFER_UAV(RWBuffer<uint32>, Counter)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment);
	
};

class VOXELMESH_API FVoxelMarchingCubesCalcCubeOffsetCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelMarchingCubesCalcCubeOffsetCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelMarchingCubesCalcCubeOffsetCS, FGlobalShader);
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FVoxelMarchingCubeUniformParameters, MarchingCubeParameters)
		VOXEL_SHADER_PARAMETER_BUFFER_SRV(StructuredBuffer<uint32>, SrcVoxelData)
		VOXEL_SHADER_PARAMETER_BUFFER_SRV(Buffer<uint32>, InCubeIndexOffsets)
		VOXEL_SHADER_PARAMETER_BUFFER_UAV(RWBuffer<uint32>, Counter)
		// The creation of these resource will be delayed. So it don't managed by render graph.
		SHADER_PARAMETER_UAV(RWBuffer<uint32>, OutNonEmptyCubeLinearId)
		SHADER_PARAMETER_UAV(RWBuffer<uint32>, OutNonEmptyCubeIndex)
		SHADER_PARAMETER_UAV(RWBuffer<uint32>, OutVertexIndexOffset)
	END_SHADER_PARAMETER_STRUCT()
};

class VOXELMESH_API FVoxelMarchingCubesGenerateMeshCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelMarchingCubesGenerateMeshCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelMarchingCubesGenerateMeshCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FVoxelMarchingCubeUniformParameters, MarchingCubeParameters)
		VOXEL_SHADER_PARAMETER_BUFFER_SRV(StructuredBuffer<uint32>, SrcVoxelData)
		VOXEL_SHADER_PARAMETER_BUFFER_SRV(Buffer<uint32>, InCubeIndexOffsets)
		// The creation of these resource will be delayed. So it don't managed by render graph.
		SHADER_PARAMETER_SRV(Buffer<uint32>, InNonEmptyCubeLinearId)
		SHADER_PARAMETER_SRV(Buffer<uint32>, InNonEmptyCubeIndex)
		SHADER_PARAMETER_SRV(Buffer<uint32>, InVertexIndexOffset)
		// RHIProxy is going to manage these resources.
		SHADER_PARAMETER_UAV(RWBuffer<float4>, OutVertexBuffer)
		SHADER_PARAMETER_UAV(RWBuffer<uint32>, OutIndexBuffer)

		SHADER_PARAMETER(uint32, NumNonEmptyCubes)
	END_SHADER_PARAMETER_STRUCT()
};
