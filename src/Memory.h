#pragma once
#include "Core.h"

namespace Memory
{
	void Init();
	void Exit();

	inline u64 GetAlignmentAdjustment(u8* raw, size_t alignment)
	{
		u64 ptr = reinterpret_cast<u64>(raw);

		u64 mask = (alignment - 1);
		u64 misalignment = 0;
		if (ptr & mask)
		{
			misalignment = alignment - (ptr & mask);
		}

		return misalignment;
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

	struct Arena
	{
		u8* m_memory_block;
		u64 m_bytes_used;
		u64 m_size;
	};

	void InitArena(Arena* arena, u64 size_bytes, u64 alignment = PLATFORM_DEFAULT_ALIGNMENT);
	void ClearArena(Arena* arena, bool zero_memory);
	void FreeArena(Arena* arena);

	struct PushParams
	{
		enum Flags
		{
			CLEAR_TO_ZERO = 1
		};

		u64 alignment;
		u32 flags;
	};

	PushParams DefaultPushParams();
	void* PushSize(Arena* arena, u64 size_bytes, PushParams push_params = DefaultPushParams());
}