#pragma once

#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

class VOXELMESH_API FVoxelMarchingCubesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelMarchingCubesCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelMarchingCubesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, DimensionX)
		SHADER_PARAMETER(uint32, DimensionY)
		SHADER_PARAMETER(uint32, DimensionZ)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, SrcVoxelData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float3>, OutVertexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutIndexBuffer)
		SHADER_PARAMETER(float, SurfaceIsoValue)
	END_SHADER_PARAMETER_STRUCT()
};
