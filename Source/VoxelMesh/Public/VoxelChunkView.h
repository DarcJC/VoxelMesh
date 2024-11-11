// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VoxelRHIUtility.h"
#include "VoxelVdbCommon.h"
#include "VoxelChunkView.generated.h"

class FVoxelMarchingCubesUniforms;
struct FVoxelChunkViewRHIProxy;

UCLASS(BlueprintType)
class VOXELMESH_API UVoxelChunkView : public UObject
{
	GENERATED_BODY()

	using FVoxelElementType = int32;

public:
	UVoxelChunkView(const FObjectInitializer& ObjectInitializer);
	virtual ~UVoxelChunkView() override;
	
	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsDirty() const;

	void MarkAsDirty();

	void SetVdbBuffer_GameThread(nanovdb::GridHandle<nanovdb::HostBuffer>&& NewBuffer);

	virtual void Serialize(FArchive& Ar) override;

	UFUNCTION(BlueprintCallable)
	void TestDispatch();

	TSharedPtr<FVoxelChunkViewRHIProxy> GetRHIProxy();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Voxel | Property")
	uint32 DimensionX;
	
	UPROPERTY(VisibleAnywhere, Category = "Voxel | Property")
	uint32 DimensionY;
	
	UPROPERTY(VisibleAnywhere, Category = "Voxel | Property")
	uint32 DimensionZ;

	nanovdb::GridHandle<nanovdb::HostBuffer> HostVdbBuffer;

private:
	TSharedPtr<FVoxelChunkViewRHIProxy> RHIProxy;
	
	friend class UVoxelRenderingWorldSubsystem;
};

struct FVoxelChunkViewRHIProxy
{

	void ResizeBuffer_RenderThread(uint32_t NewVBSize, uint32 NewIBSize)
	{
		check(IsInParallelRenderingThread());

		FRHICommandList& RHICmdList = FRHICommandListImmediate::Get();;

		if (!MeshVertexBuffer || MeshVertexBuffer->GetSize() != NewVBSize)
		{
			FRHIResourceCreateInfo BufferCreateInfo(TEXT("Voxel Vertex Buffer"));
			MeshVertexBuffer = RHICmdList.CreateVertexBuffer(NewVBSize, EBufferUsageFlags::UnorderedAccess, BufferCreateInfo);
			MeshVertexBufferUAV = RHICmdList.CreateUnorderedAccessView(MeshVertexBuffer, PF_R32G32B32A32_UINT);
		}

		if (!MeshIndexBuffer || MeshIndexBuffer->GetSize() != NewIBSize)
		{
			FRHIResourceCreateInfo BufferCreateInfo(TEXT("Voxel Index Buffer"));
			MeshIndexBuffer = RHICmdList.CreateIndexBuffer(sizeof(uint32), NewIBSize, EBufferUsageFlags::UnorderedAccess, BufferCreateInfo);
			MeshIndexBufferUAV = RHICmdList.CreateUnorderedAccessView(MeshIndexBuffer, PF_R32_UINT);
		}
	}
	
	TRefCountPtr<FRHIBuffer> MeshVertexBuffer;
	TRefCountPtr<FRHIBuffer> MeshIndexBuffer;
	TRefCountPtr<FRHIUnorderedAccessView> MeshVertexBufferUAV;
	TRefCountPtr<FRHIUnorderedAccessView> MeshIndexBufferUAV;
};
