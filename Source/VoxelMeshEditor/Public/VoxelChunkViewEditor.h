// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "UObject/Object.h"
#include "VoxelChunkViewEditor.generated.h"

UENUM()
enum class EVoxelGridType : uint8
{
	Sphere UMETA(DisplayName = "Sphere"),
	Box UMETA(DisplayName = "Box"),
	Torus UMETA(DisplayName = "Torus"),
	Octahedron UMETA(DisplayName = "Octahedron")
};

UCLASS()
class UVoxelDataCreationOptions : public UObject
{
	GENERATED_BODY()

public:
	// Common parameters
	UPROPERTY(EditAnywhere, Category = "Common")
	EVoxelGridType GridType = EVoxelGridType::Sphere;
	
	UPROPERTY(EditAnywhere, Category = "Common")
	double VoxelSize = 1.0;
	
	UPROPERTY(EditAnywhere, Category = "Common", meta = (ClampMin = "1.0"))
	double Radius = 100.0;
	
	UPROPERTY(EditAnywhere, Category = "Common")
	FVector Center = FVector(100.0f, 100.0f, 100.0f);

	// Sphere specific parameters
	// (uses common radius and center parameters)

	// Box specific parameters
	UPROPERTY(EditAnywhere, Category = "Box", meta = (EditCondition = "GridType == EVoxelGridType::Box", EditConditionHides, ClampMin = "1.0"))
	double Width = 40.0;
	
	UPROPERTY(EditAnywhere, Category = "Box", meta = (EditCondition = "GridType == EVoxelGridType::Box", EditConditionHides, ClampMin = "1.0"))
	double Height = 60.0;
	
	UPROPERTY(EditAnywhere, Category = "Box", meta = (EditCondition = "GridType == EVoxelGridType::Box", EditConditionHides, ClampMin = "1.0"))
	double Depth = 100.0;
	
	UPROPERTY(EditAnywhere, Category = "Box", meta = (EditCondition = "GridType == EVoxelGridType::Box", EditConditionHides, ClampMin = "0.1"))
	double HalfWidth = 3.0;

	// Torus specific parameters
	UPROPERTY(EditAnywhere, Category = "Torus", meta = (EditCondition = "GridType == EVoxelGridType::Torus", EditConditionHides, ClampMin = "1.0"))
	double MajorRadius = 100.0;
	
	UPROPERTY(EditAnywhere, Category = "Torus", meta = (EditCondition = "GridType == EVoxelGridType::Torus", EditConditionHides, ClampMin = "1.0"))
	double MinorRadius = 40.0;
};

UCLASS()
class VOXELMESHEDITOR_API UVoxelChunkViewFactory : public UFactory
{
	GENERATED_BODY()
public:
	UVoxelChunkViewFactory(const FObjectInitializer& Initializer);

	// Begin UObject interface
	virtual void PostInitProperties() override;
	// End UObject interface

	// Begin UFactory interface
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End UFactory interface

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Mesh Editor")
	UVoxelDataCreationOptions* VoxelDataCreationOptions;

protected:
	static bool ShowVoxelCreationDialog(UVoxelDataCreationOptions* OutOptions);
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

