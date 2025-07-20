// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
#include "AssetTypeActions_Base.h"
#endif // WITH_EDITOR

#include "UObject/Object.h"
#include "VoxelRHIUtility.h"
#include "VoxelVdbCommon.h"
#include "VoxelChunkView.generated.h"

class FVoxelMarchingCubesUniforms;
struct FVoxelChunkViewRHIProxy;

DECLARE_MULTICAST_DELEGATE(FVoxelChunkMeshBuildFinishedDelegate);

// Forward declare processing resources struct
struct FVoxelProcessingResources
{
	TRefCountPtr<FRHIBuffer> NonEmptyCubeLinearIdBuffer = nullptr;
	TRefCountPtr<FRHIUnorderedAccessView> NonEmptyCubeLinearIdBufferUAV = nullptr;
	TRefCountPtr<FRHIShaderResourceView> NonEmptyCubeLinearIdBufferSRV = nullptr;
	
	TRefCountPtr<FRHIBuffer> NonEmptyCubeIndexBuffer = nullptr;
	TRefCountPtr<FRHIUnorderedAccessView> NonEmptyCubeIndexBufferUAV = nullptr;
	TRefCountPtr<FRHIShaderResourceView> NonEmptyCubeIndexBufferSRV = nullptr;
	
	TRefCountPtr<FRHIBuffer> VertexIndexOffsetBuffer = nullptr;
	TRefCountPtr<FRHIUnorderedAccessView> VertexIndexOffsetBufferUAV = nullptr;
	TRefCountPtr<FRHIShaderResourceView> VertexIndexOffsetBufferSRV = nullptr;
	
	uint32 EstimatedNonEmptyCubes = 0;
	
	bool AreResourcesValid() const
	{
		return NonEmptyCubeLinearIdBuffer && NonEmptyCubeLinearIdBufferUAV && NonEmptyCubeLinearIdBufferSRV &&
			   NonEmptyCubeIndexBuffer && NonEmptyCubeIndexBufferUAV && NonEmptyCubeIndexBufferSRV &&
			   VertexIndexOffsetBuffer && VertexIndexOffsetBufferUAV && VertexIndexOffsetBufferSRV;
	}
};

// Mesh generation modes
UENUM(BlueprintType)
enum class EVoxelMeshGenerationMode : uint8
{
	// Always allocate maximum buffer size (faster, uses more memory)
	PerformanceOptimized UMETA(DisplayName = "Performance Optimized"),
	
	// Read counter buffer to allocate exact buffer size (slower, saves memory)
	MemoryOptimized UMETA(DisplayName = "Memory Optimized")
};

UCLASS(BlueprintType, EditInlineNew)
class VOXELMESH_API UVoxelChunkView : public UObject
{
	GENERATED_BODY()

	using FVoxelElementType = int32;

public:
	UVoxelChunkView(const FObjectInitializer& ObjectInitializer);
	virtual ~UVoxelChunkView() override;
	
	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsDirty() const;

	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsEmpty() const;

	UFUNCTION(BlueprintCallable)
	void RebuildMesh();

	/** Force use of async compute for mesh generation (for testing) */
	UFUNCTION(BlueprintCallable, Category = "Voxel|Debug")
	void RebuildMeshAsync();

	/** Force use of synchronous compute for mesh generation (for testing) */
	UFUNCTION(BlueprintCallable, Category = "Voxel|Debug")  
	void RebuildMeshSync();

	TSharedPtr<FVoxelChunkViewRHIProxy> GetRHIProxy();

	void MarkAsDirty();

	void SetVdbBuffer_GameThread(nanovdb::GridHandle<nanovdb::HostBuffer>&& NewBuffer);

	UFUNCTION(BlueprintSetter)
	void UpdateSurfaceIsoValue(float NewValue);

	virtual void Serialize(FArchive& Ar) override;

	FVoxelChunkMeshBuildFinishedDelegate OnBuildFinished;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	
	UPROPERTY(EditAnywhere, BlueprintSetter=UpdateSurfaceIsoValue, Category = "Voxel")
	float SurfaceIsoValue = 0.0f;

	/** The mesh generation mode to use */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	EVoxelMeshGenerationMode MeshGenerationMode = EVoxelMeshGenerationMode::PerformanceOptimized;
	
	/** Get the current mesh generation mode */
	UFUNCTION(BlueprintCallable, Category = "Voxel")
	EVoxelMeshGenerationMode GetGenerationMode() const { return MeshGenerationMode; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Voxel | Debug")
	uint32 DimensionX;
	
	UPROPERTY(VisibleAnywhere, Category = "Voxel | Debug")
	uint32 DimensionY;
	
	UPROPERTY(VisibleAnywhere, Category = "Voxel | Debug")
	uint32 DimensionZ;

	nanovdb::GridHandle<nanovdb::HostBuffer> HostVdbBuffer;

	UPROPERTY()
	TArray<uint8> VdbBulkData;

private:
	TSharedPtr<FVoxelChunkViewRHIProxy> RHIProxy;
	
	friend class UVoxelRenderingWorldSubsystem;
	friend struct FVoxelChunkViewRHIProxy;
};

struct FVoxelChunkViewRHIProxy
{
	explicit FVoxelChunkViewRHIProxy(const UVoxelChunkView* ChunkView);

	void ResizeBuffer_RenderThread(uint32_t NewVBSize, uint32 NewIBSize);
	void RegenerateMesh_RenderThread(FRHICommandListImmediate& RHICmdList);
	void RegenerateMesh_GameThread();
	void RegenerateMesh();

	// New async compute methods
	void RegenerateMeshAsync_GameThread();
	void RegenerateMeshAsync_RenderThread();
	void RegenerateMeshAsyncCompute_RenderThread(FRHIAsyncComputeCommandListImmediate& AsyncComputeCmdList);
	void FinalizeMeshGenerationAsync(FRHIAsyncComputeCommandListImmediate& AsyncComputeCmdList, uint32 NumNonEmptyCubes, 
		const FVoxelProcessingResources& Resources, 
		TUniformBufferRef<FVoxelMarchingCubeUniformParameters> UniformParametersBuffer,
		FShaderResourceViewRHIRef GridBufferSRV,
		FShaderResourceViewRHIRef CubeIndexOffsetBufferSRV);

	bool IsReady() const;
	bool IsGenerating() const;

	TObjectPtr<UVoxelChunkView> Parent;
	TRefCountPtr<FRHIBuffer> MeshVertexBuffer;
	TRefCountPtr<FRHIBuffer> MeshIndexBuffer;
	TRefCountPtr<FRHIUnorderedAccessView> MeshVertexBufferUAV;
	TRefCountPtr<FRHIUnorderedAccessView> MeshIndexBufferUAV;
	TArray<uint8> VoxelDataBuffer;
	
	// 替换单一的VoxelSize为三个独立的维度
	uint32 VoxelSizeX;
	uint32 VoxelSizeY;
	uint32 VoxelSizeZ;
	
	float SurfaceIsoValue = 0.0f;
	std::atomic<bool> bIsReady;

	// Async compute state management
	std::atomic<bool> bIsAsyncGenerating{false};
	TRefCountPtr<FRHIGPUFence> AsyncComputeFence;
	
	// Performance tracking
	double AsyncStartTime = 0.0;
};

