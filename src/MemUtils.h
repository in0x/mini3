#pragma once
#include "Core.h"

namespace Memory
{
	inline u64 GetAlignmentAdjustment(u8* raw, size_t alignment)
	{
		u64 ptr = reinterpret_cast<u64>(raw);

		u64 mask = (alignment - 1);
		u64 misalignment = (ptr & mask);
		return alignment - misalignment;
	}

	inline u8* AlignAddress(u8* raw, size_t alignment)
	{
		u64 adjust = GetAlignmentAdjustment(raw, alignment);

		u8* aligned = raw + adjust;
		return aligned;
	}

	inline u64 AlignValue(u64 value, size_t alignment)
	{
		u64 mask = (alignment - 1);
		u64 misalignment = (value & mask);
		return value + (alignment - misalignment);
	}
}