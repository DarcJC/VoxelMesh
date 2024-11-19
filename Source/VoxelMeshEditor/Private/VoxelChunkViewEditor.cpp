// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelChunkViewEditor.h"

#include "VoxelChunkView.h"
#include "VoxelUtilities.h"


UVoxelChunkViewFactory::UVoxelChunkViewFactory()
{
	SupportedClass = UVoxelChunkView::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UVoxelChunkViewFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	UObject* Context, FFeedbackContext* Warn)
{
	check(InClass == UVoxelChunkView::StaticClass());
	
	UVoxelChunkView* NewObject = UVoxelUtilities::CreateSphereChunkView(InParent);
	NewObject->SetFlags(Flags);
	NewObject->Rename(*InName.ToString(), InParent);

	return NewObject;
}

FText FVoxelChunkAssetTypeActions::GetName() const
{
	return NSLOCTEXT("VoxelMesh", "Voxel Mesh Assets", "Voxel Mesh Assets");
}

FColor FVoxelChunkAssetTypeActions::GetTypeColor() const
{
	return FColor::Cyan;
}

UClass* FVoxelChunkAssetTypeActions::GetSupportedClass() const
{
	return UVoxelChunkView::StaticClass();
}

bool FVoxelChunkAssetTypeActions::HasActions(const TArray<UObject*>& InObjects) const
{
	return false;
}

void FVoxelChunkAssetTypeActions::GetActions(const TArray<UObject*>& InObjects, class FMenuBuilder& MenuBuilder)
{
}

uint32 FVoxelChunkAssetTypeActions::GetCategories()
{
	return EAssetTypeCategories::Basic;
}
