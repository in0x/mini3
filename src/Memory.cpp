#include "Memory.h"
#include "Win32.h"

namespace Memory
{
	void InitArena(Arena* arena, u64 size_bytes, u64 alignment)
	{
		MemZeroSafe(arena);

		// Nothing fancy, we just always commit. If we need anything
		// more complex than this, we'll solve it bespokely.
		u32 alloc_type = MEM_RESERVE | MEM_COMMIT;
		u64 aligned_size = size_bytes;

		// If we're over a large page, we'll allocate in large pages. Otherwise we 
		// round up to a normal page, since we'll be taking that amount always anyways.
		u64 large_page_size = QueryLargePageSize();
		if (large_page_size && size_bytes > large_page_size)
		{
			alloc_type |= MEM_LARGE_PAGES;
			aligned_size = AlignValue(size_bytes, large_page_size);
		}
		else
		{
			aligned_size = AlignValue(size_bytes, alignment);
			aligned_size = max(aligned_size, QuerySmallPageSize());
		}

		u8* allocation = (u8*)VirtualAlloc(0, aligned_size, alloc_type, PAGE_READWRITE);

		if (allocation == nullptr)
		{
			LogLastWindowsError();
			ASSERT_FAIL_F("Failed to allocate memory for arena!");
			return;
		}

		arena->m_memory_block = allocation;
		arena->m_bytes_used = 0;
		arena->m_size = aligned_size;
	}

	void ClearArena(Arena* arena, bool zero_memory)
	{
		arena->m_bytes_used = 0;
		if (zero_memory)
		{
			memzero(arena->m_memory_block, arena->m_size);
		}
	}

	void FreeArena(Arena* arena)
	{
		VirtualFree(arena->m_memory_block, 0, MEM_RELEASE);
		MemZeroSafe(arena);
	}

	TemporaryAllocation BeginTemporaryAlloc(Arena* arena)
	{
		TemporaryAllocation alloc;
		alloc.m_start = arena->m_bytes_used;

		return alloc;
	}
	
	void RewindTemporaryAlloc(Arena* arena, TemporaryAllocation alloc, bool zero_memory)
	{
		u64 alloc_size = arena->m_bytes_used - alloc.m_start;
		if (alloc_size > 0)
		{
			arena->m_bytes_used = alloc.m_start;
			if (zero_memory)
			{
				memzero(arena->m_memory_block, alloc_size);
			}
		}
	}

	PushParams DefaultPushParams()
	{
		PushParams params;
		params.alignment = PLATFORM_DEFAULT_ALIGNMENT;
		params.flags = 0;

		return params;
	}

	PushParams ZeroPush()
	{
		PushParams params;
		params.alignment = PLATFORM_DEFAULT_ALIGNMENT;
		params.flags = PushParams::CLEAR_TO_ZERO;

		return params;
	}

	PushParams ZeroAndAlignPush(u64 alignment)
	{
		PushParams params;
		params.alignment = alignment;
		params.flags = PushParams::CLEAR_TO_ZERO;

		return params;
	}

	PushParams AlignPush(u64 alignment)
	{
		PushParams params;
		params.alignment = alignment;
		params.flags = 0;

		return params;

	}

	void* PushSize(Arena* arena, u64 size_bytes, PushParams push_params)
	{
		ASSERT(IsPow2(push_params.alignment));
		if (size_bytes == 0)
		{
			ASSERT_FAIL_F("Attempted a 0 alloc!");
			return nullptr;
		}

		u8* arena_top = arena->m_memory_block + arena->m_bytes_used;
		u64 align_offset = GetAlignmentAdjustment(arena_top, push_params.alignment);
		u64 aligned_size = size_bytes + align_offset;

		if ((aligned_size + arena->m_bytes_used) > arena->m_size)
		{
			// TODO(): This is pretty simplistic, could add ability for arena to grow.
			ASSERT_FAIL_F("Tried to allocate more than arena had capacity for!");
			return nullptr;
		}

		void* allocation = (void*)(arena->m_memory_block + arena->m_bytes_used + align_offset);
		arena->m_bytes_used += aligned_size;

		if (push_params.flags & PushParams::CLEAR_TO_ZERO)
		{
			memzero(allocation, size_bytes);
		}

		return allocation;
	}
}