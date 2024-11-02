// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelRenderingWorldSubsystem.h"

#include "EngineUtils.h"
#include "SceneViewExtension.h"
#include "VoxelChunkView.h"
#include "VoxelMeshComponent.h"
#include "VoxelViewExtension.h"

void UVoxelRenderingWorldSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateCurrentChunks_GameThread();

	for (UVoxelChunkView* Chunk : Chunks)
	{
		if (Chunk->IsDirty())
		{
		}
	}
}

void UVoxelRenderingWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	UWorld* World = GetWorld();
	check(nullptr != World);

	VoxelViewExtension = FSceneViewExtensions::NewExtension<FVoxelViewExtension>(World);
	
	Super::Initialize(Collection);
}

void UVoxelRenderingWorldSubsystem::Deinitialize()
{
	Super::Deinitialize();

	Chunks.Reset();
	VoxelViewExtension.Reset();
}

TStatId UVoxelRenderingWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVoxelRenderingWorldSubsystem, STATGROUP_Tickables);
}

UVoxelRenderingWorldSubsystem* UVoxelRenderingWorldSubsystem::Get(const UWorld* World)
{
	check(nullptr != World);
	return World->GetSubsystem<UVoxelRenderingWorldSubsystem>();
}

void UVoxelRenderingWorldSubsystem::UpdateCurrentChunks_GameThread()
{
	UWorld* World = GetWorld();
	check(nullptr != World);
	Chunks.Reset();
}

TArrayView<UVoxelChunkView*> UVoxelRenderingWorldSubsystem::GetChunks()
{
	return Chunks;
}
