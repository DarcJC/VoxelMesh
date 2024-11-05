// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelChunkView.h"

#include "ImageUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "VoxelRenderingWorldSubsystem.h"
#include "VoxelShaders.h"
#include <iostream>

#include "SystemTextures.h"
#include "Engine/TextureRenderTarget2D.h"

UVoxelChunkView::UVoxelChunkView(const FObjectInitializer& ObjectInitializer)
{
	DimensionX = 1;
	DimensionY = 1;
	DimensionZ = 1;
	RenderTarget_Debug = ObjectInitializer.CreateDefaultSubobject<UTextureRenderTarget2D>(this, TEXT("DebugRenderTarget"));
}

UVoxelChunkView::~UVoxelChunkView()
{
}

bool UVoxelChunkView::IsDirty() const
{
	return bRequireRebuild;
}

void UVoxelChunkView::MarkAsDirty()
{
	bRequireRebuild = true;
}

void UVoxelChunkView::SetVdbBuffer_GameThread(nanovdb::GridHandle<nanovdb::HostBuffer>&& NewBuffer)
{
	MarkAsDirty();
	HostVdbBuffer = MoveTemp(NewBuffer);
	if (HostVdbBuffer)
	{
		const auto& Grid = HostVdbBuffer.grid<float>();
		const auto& Bbox = Grid->indexBBox();
		const auto& BboxMin = Bbox.min();
		const auto& BboxMax = Bbox.max();
		DimensionX = BboxMax.x() - BboxMin.x() + 1;
		DimensionY = BboxMax.y() - BboxMin.y() + 1;
		DimensionZ = BboxMax.z() - BboxMin.z() + 1;
	}
	else
	{
		DimensionX = 0;
		DimensionY = 0;
		DimensionZ = 0;
	}
}

void UVoxelChunkView::Serialize(FArchive& Ar)
{
	UObject::Serialize(Ar);

	if (Ar.IsLoading())
	{
		MarkAsDirty();
	}

	FBulkDataBuffer<uint8> Buffer;
}

void UVoxelChunkView::TestDispatch()
{
	ENQUEUE_RENDER_COMMAND(QwQVoxel)([this] (FRHICommandListImmediate& CmdList)
	{
		// FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		// check(ShaderMap);
		// auto CSRef = ShaderMap->GetShader<FVoxelMarchingCubesCS>();
		//
		// FRHIComputeCommandList& ComputeCommandList = FRHIComputeCommandList::Get(CmdList);
		// FVoxelMarchingCubesCS::FParameters Parameters{};
		//
		// Parameters.DimensionX = DimensionX;
		// Parameters.DimensionY = DimensionY;
		// Parameters.DimensionZ = DimensionZ;
		// Parameters.SurfaceIsoValue = 0.0f;
		//
		// nanovdb::NanoGrid<float>* GridData = HostVdbBuffer.grid<float>();
		// FRHIResourceCreateInfo GirdBufferCreateInfo(TEXT("VoxelVdbData"));
		// FVoxelResourceArrayUploadArrayView UploadArrayView(GridData, HostVdbBuffer.size());
		// GirdBufferCreateInfo.ResourceArray = &UploadArrayView;
		// FBufferRHIRef SrcVoxelBuffer = CmdList.CreateStructuredBuffer(sizeof(uint32), HostVdbBuffer.size(), EBufferUsageFlags::Static | EBufferUsageFlags::UnorderedAccess, ERHIAccess::SRVMask, GirdBufferCreateInfo);
		// Parameters.SrcVoxelData = CmdList.CreateShaderResourceView(SrcVoxelBuffer);
		//
		// FRHIResourceCreateInfo OutVertexBufferCreateInfo(TEXT("VoxelVertexBuffer"));
		// FBufferRHIRef OutVertexBuffer = CmdList.CreateBuffer(sizeof(float) * 9 * 15 * DimensionX * DimensionY * DimensionZ, EBufferUsageFlags::VertexBuffer | EBufferUsageFlags::UnorderedAccess, sizeof(float), ERHIAccess::UAVMask, OutVertexBufferCreateInfo);
		// Parameters.OutVertexBuffer = CmdList.CreateUnorderedAccessView(OutVertexBuffer, PF_R32_FLOAT);
		//
		// FRHIResourceCreateInfo OutIndexBufferCreateInfo(TEXT("VoxelIndexBuffer"));
		// FBufferRHIRef OutIndexBuffer = CmdList.CreateBuffer(sizeof(uint32) * 3 * 15 * DimensionX * DimensionY * DimensionZ, EBufferUsageFlags::IndexBuffer | EBufferUsageFlags::UnorderedAccess, sizeof(float), ERHIAccess::UAVMask, OutIndexBufferCreateInfo);
		// Parameters.OutIndexBuffer = CmdList.CreateUnorderedAccessView(OutIndexBuffer, PF_R32_UINT);
		//
		// FComputeShaderUtils::Dispatch(ComputeCommandList, CSRef, Parameters, FIntVector { FMath::CeilToInt(DimensionX / 1.f), FMath::CeilToInt(DimensionY / 1.f), FMath::CeilToInt(DimensionZ / 1.f) });
		//
		// CmdList.SubmitAndBlockUntilGPUIdle();
		//
		// TArray<float> VertexBufferData{};
		// TArray<uint32> IndexBufferData{};
		// VertexBufferData.AddUninitialized(OutVertexBuffer->GetSize());
		// IndexBufferData.AddUninitialized(OutIndexBuffer->GetSize());
		//
		// FRHIGPUBufferReadback VertexReadback{TEXT("VoxelVertexReadback")};
		// FRHIGPUBufferReadback IndexReadback{TEXT("VoxelIndexReadback")};
		// VertexReadback.EnqueueCopy(CmdList, OutVertexBuffer, 0);
		// IndexReadback.EnqueueCopy(CmdList, OutIndexBuffer, 0);
		//
		// FMemory::Memcpy(VertexBufferData.GetData(), VertexReadback.Lock(OutVertexBuffer->GetSize()), OutVertexBuffer->GetSize());
		// FMemory::Memcpy(IndexBufferData.GetData(), IndexReadback.Lock(OutIndexBuffer->GetSize()), OutIndexBuffer->GetSize());
		// VertexReadback.Unlock();
		// IndexReadback.Unlock();
	});
}
