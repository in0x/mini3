#include "Memory.h"
#include "Win32.h"

namespace Memory
{
	struct MemorySystem
	{
		u64 m_system_page_size;
	};

	// If this needs to become more complex, we'll want to put it on the heap.
	static MemorySystem s_memory_system;

	void Init()
	{
		_SYSTEM_INFO info;
		GetSystemInfo(&info);

		s_memory_system.m_system_page_size = info.dwPageSize;
	}

	void Exit()
	{
	}

	void InitArena(Arena* arena, u64 size_bytes, u64 alignment)
	{
		u64 aligned_size = AlignValue(size_bytes, alignment);
		aligned_size = max(aligned_size, s_memory_system.m_system_page_size); // NOTE(): Might as well round up to a whole page to reduce waste.

		u8* allocation = (u8*)VirtualAlloc(0, aligned_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

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
	}

	PushParams DefaultPushParams()
	{
		PushParams params;
		params.alignment = PLATFORM_DEFAULT_ALIGNMENT;
		params.flags = 0;

		return params;
	}

	void* PushSize(Arena* arena, u64 size_bytes, PushParams push_params)
	{
		ASSERT(IsPow2(push_params.alignment));

		u8* arena_top = arena->m_memory_block + arena->m_bytes_used;
		u64 align_offset = GetAlignmentAdjustment(arena_top, push_params.alignment);
		u64 aligned_size = size_bytes += align_offset;

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