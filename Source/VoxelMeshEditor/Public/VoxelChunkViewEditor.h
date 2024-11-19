// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "UObject/Object.h"
#include "VoxelChunkViewEditor.generated.h"

UCLASS()
class VOXELMESHEDITOR_API UVoxelChunkViewFactory : public UFactory
{
	GENERATED_BODY()
public:
	UVoxelChunkViewFactory();

	// Begin UFactory interface
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End UFactory interface
};

class VOXELMESHEDITOR_API FVoxelChunkAssetTypeActions : public FAssetTypeActions_Base
{
public:
	// Begin IAssetTypeActions interface
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual bool HasActions(const TArray<UObject*>& InObjects) const override;
	virtual void GetActions(const TArray<UObject*>& InObjects, class FMenuBuilder& MenuBuilder) override;
	virtual uint32 GetCategories() override;
	// End IAssetTypeActions interface
};

