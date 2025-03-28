﻿// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelChunkViewEditor.h"

#include "VoxelChunkView.h"
#include "VoxelUtilities.h"
#include "Editor/PropertyEditor/Private/SDetailsView.h"
#include "Interfaces/IMainFrameModule.h"


UVoxelChunkViewFactory::UVoxelChunkViewFactory(const FObjectInitializer& Initializer)
{
	SupportedClass = UVoxelChunkView::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
	VoxelDataCreationOptions = Initializer.CreateDefaultSubobject<UVoxelDataCreationOptions>(this, "Options", true);
}

void UVoxelChunkViewFactory::PostInitProperties()
{
	Super::PostInitProperties();
}

UObject* UVoxelChunkViewFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
                                                  UObject* Context, FFeedbackContext* Warn)
{
	check(InClass == UVoxelChunkView::StaticClass());
	
	ShowVoxelCreationDialog(VoxelDataCreationOptions);

	// Convert FVector to nanovdb Vec3d for center
	nanovdb::Vec3d Center(
		VoxelDataCreationOptions->Center.X, 
		VoxelDataCreationOptions->Center.Y, 
		VoxelDataCreationOptions->Center.Z
	);

	// Initialize grid with default value
	nanovdb::GridHandle<nanovdb::HostBuffer> NewGrid;

	// Create appropriate grid based on selected type
	switch (VoxelDataCreationOptions->GridType)
	{
	case EVoxelGridType::Sphere:
		NewGrid = nanovdb::tools::createLevelSetSphere<nanovdb::Fp4, nanovdb::HostBuffer>(
			VoxelDataCreationOptions->Radius,
			nanovdb::Vec3f(Center), // Convert Vec3d to Vec3f if needed for this function
			VoxelDataCreationOptions->VoxelSize);
		break;
		
	case EVoxelGridType::Box:
		NewGrid = nanovdb::tools::createLevelSetBox<nanovdb::Fp4, nanovdb::HostBuffer>(
			VoxelDataCreationOptions->Width,
			VoxelDataCreationOptions->Height,
			VoxelDataCreationOptions->Depth,
			Center,
			VoxelDataCreationOptions->VoxelSize,
			VoxelDataCreationOptions->HalfWidth);
		break;
		
	case EVoxelGridType::Torus:
		NewGrid = nanovdb::tools::createLevelSetTorus<nanovdb::Fp4, nanovdb::HostBuffer>(
			VoxelDataCreationOptions->MajorRadius,
			VoxelDataCreationOptions->MinorRadius,
			nanovdb::Vec3f(Center), // Convert Vec3d to Vec3f if needed for this function
			VoxelDataCreationOptions->VoxelSize);
		break;
		
	case EVoxelGridType::Octahedron:
		NewGrid = nanovdb::tools::createLevelSetOctahedron<nanovdb::Fp4, nanovdb::HostBuffer>(
			VoxelDataCreationOptions->Radius,
			nanovdb::Vec3f(Center), // Convert Vec3d to Vec3f if needed for this function
			VoxelDataCreationOptions->VoxelSize);
		break;
	}

	UVoxelChunkView* NewView = NewObject<UVoxelChunkView>(InParent, InClass, InName, Flags);
	NewView->SetVdbBuffer_GameThread(MoveTemp(NewGrid));

	return NewView;
}

bool UVoxelChunkViewFactory::ShowVoxelCreationDialog(UVoxelDataCreationOptions* OutOptions)
{
	if (!IsValid(OutOptions))
	{
		return false;
	}
	
	TSharedPtr<SWindow> ParentWindow;

	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
	}

	const float ImportWindowWidth = 450.0f;
	const float ImportWindowHeight = 750.0f;
	FVector2D ImportWindowSize = FVector2D(ImportWindowWidth, ImportWindowHeight); // Max window size it can get based on current slate
	
	FSlateRect WorkAreaRect = FSlateApplicationBase::Get().GetPreferredWorkArea();
	FVector2D DisplayTopLeft(WorkAreaRect.Left, WorkAreaRect.Top);
	FVector2D DisplaySize(WorkAreaRect.Right - WorkAreaRect.Left, WorkAreaRect.Bottom - WorkAreaRect.Top);

	FVector2D WindowPosition = (DisplayTopLeft + (DisplaySize - ImportWindowSize) / 2.0f);
	
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(NSLOCTEXT("VoxelMesh", "VoxelMeshCreationOptionsTitle", "Voxel Creation Options"))
		.SizingRule(ESizingRule::Autosized)
		.AutoCenter(EAutoCenter::None)
		.ClientSize(ImportWindowSize)
		.ScreenPosition(WindowPosition);

	TSharedRef<SVerticalBox> VerticalBox = SNew(SVerticalBox);
	Window->SetContent(VerticalBox);

	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	TSharedRef<IDetailsView> PropertiesDetailView = PropertyModule.CreateDetailView(Args);
	PropertiesDetailView->SetObject(OutOptions);
	VerticalBox->AddSlot().AutoHeight().AttachWidget(PropertiesDetailView);

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

	return true;
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
