#pragma once

#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVoxelMarchingCubesUniforms, VOXELMESH_API)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, SrcVoxelData)
	SHADER_PARAMETER(uint32, DimensionX)
	SHADER_PARAMETER(uint32, DimensionY)
	SHADER_PARAMETER(uint32, DimensionZ)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

class VOXELMESH_API FVoxelMarchingCubesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelMarchingCubesCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelMarchingCubesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVoxelMarchingCubesUniforms, FieldInfo)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float3>, VertexPositions)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, TriangleIndices)
	END_SHADER_PARAMETER_STRUCT()
};
