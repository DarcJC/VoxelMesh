// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelChunkView.h"

#include "ImageUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "VoxelRenderingWorldSubsystem.h"
#include "VoxelShaders.h"

UVoxelChunkView::UVoxelChunkView(const FObjectInitializer& ObjectInitializer)
{
	DimensionX = 16;
	DimensionY = 16;
	DimensionZ = 16;
}

UVoxelChunkView::~UVoxelChunkView()
{
}

bool UVoxelChunkView::IsDirty() const
{
	return bRequireRebuild;
}
