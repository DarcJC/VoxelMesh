// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VoxelRenderingWorldSubsystem.generated.h"


UCLASS(BlueprintType, Blueprintable)
class VOXELMESH_API UVoxelRenderingWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual TStatId GetStatId() const override;

	static UVoxelRenderingWorldSubsystem* Get(const UWorld* World);

	void UpdateCurrentChunks_GameThread();
	TArrayView<class UVoxelChunkView*> GetChunks();

protected:
	TSharedPtr<class FVoxelViewExtension> VoxelViewExtension;

	UPROPERTY()
	TArray<class UVoxelChunkView*> Chunks;
};
