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
