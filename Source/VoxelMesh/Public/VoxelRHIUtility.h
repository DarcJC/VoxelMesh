#pragma once

#include "Containers/ResourceArray.h"


struct FVoxelResourceArrayUploadArrayView : public FResourceArrayUploadInterface
{
	void* Data;
	uint32 SizeInBytes;

	FVoxelResourceArrayUploadArrayView(void* InData, uint32 InSizeInBytes)
		: Data(InData)
		, SizeInBytes(InSizeInBytes)
	{
	}

	template<typename ElementType>
	FVoxelResourceArrayUploadArrayView(TConstArrayView<ElementType> View)
		: Data(const_cast<ElementType*>(View.GetData()))
		, SizeInBytes(View.Num() * View.GetTypeSize())
	{
	}

	template<typename ElementType, typename AllocatorType>
	FVoxelResourceArrayUploadArrayView(const TArray<ElementType, AllocatorType>& InArray)
		: FVoxelResourceArrayUploadArrayView(MakeArrayView(InArray))
	{
	}

	FVoxelResourceArrayUploadArrayView& operator=(FVoxelResourceArrayUploadArrayView&& Other) noexcept
	{
		if (&Other != this)
		{
			Swap(Other.Data, Data);
			Swap(SizeInBytes, Other.SizeInBytes);
		}
		return *this;
	}

	virtual const void* GetResourceData() const final
	{
		return Data;
	}

	virtual uint32 GetResourceDataSize() const final
	{
		return SizeInBytes;
	}

	virtual void Discard() final
	{
	}
};

FORCEINLINE_DEBUGGABLE static FIntVector GetDispatchSize(size_t TotalCount)
{
	// There is a dimension size limitation in DX11, DX12, OpenGL.
	if (TotalCount <= UINT16_MAX)
	{
		return { static_cast<int>(TotalCount), 1, 1 };
	}

	constexpr int32_t DimensionX = 65535U;
	const int32_t DimensionY = FMath::CeilToInt32(TotalCount / static_cast<float>(DimensionX));
	constexpr int32_t DimensionZ = 1;

	return { DimensionX, DimensionY, DimensionZ };
}
