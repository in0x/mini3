#pragma once
#include "Core.h"
#include "Memory.h"

#include <io.h>

#define CGLTF_IMPLEMENTATION
#include "external/cgltf/cgltf.h"

namespace Mini
{
	struct SceneImporter
	{
		char const* file_path;
		Memory::Arena* scratch_arena;
	};

	static u8* ReadFileBuffer(char const* file_path, Memory::Arena* buffer_allocator, u64* bytes_read)
	{
		*bytes_read = 0;

		HANDLE fstream = CreateFile(file_path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (fstream == INVALID_HANDLE_VALUE)
		{
			ASSERT("Failed to open file %s!");
			return nullptr;
		}

		ON_SCOPE_EXIT(CloseHandle(fstream));

		LARGE_INTEGER file_size_out;
		if (!GetFileSizeEx(fstream, &file_size_out))
		{
			LogLastWindowsError();
			ASSERT("Failed to get file size!");
			return nullptr;
		}

		u64 stream_len = file_size_out.QuadPart;
		if (stream_len == 0)
		{
			ASSERT("File stream is empty!");
			return nullptr;
		}

		u8* file_data = (u8*)Memory::PushSize(buffer_allocator, stream_len, Memory::ZeroPush());

		DWORD num_bytes_read = 0;
		if (!ReadFile(fstream, file_data, stream_len, &num_bytes_read, nullptr))
		{
			LogLastWindowsError();
			ASSERT_FAIL_F("Failed to read file from stream!");
			return nullptr;
		}

		if (num_bytes_read != stream_len)
		{
			ASSERT_FAIL_F("Did not read the expected number of bytes from stream!");
			return nullptr;
		}

		*bytes_read = stream_len;
		return file_data;
	}

	static void Import(SceneImporter* importer)
	{
		Memory::TemporaryAllocation alloc = BeginTemporaryAlloc(importer->scratch_arena);

		u64 stream_len;
		u8* file_data = ReadFileBuffer(importer->file_path, importer->scratch_arena, &stream_len);
		if (file_data == nullptr)
		{
			Memory::RewindTemporaryAlloc(importer->scratch_arena, alloc, true);
			return;
		}

		cgltf_options options;
		MemZeroSafe(options);

		// TODO(): Read the .bin buffer file to get external buffers.
		cgltf_data* scene_data;
		cgltf_result result = cgltf_parse(&options, file_data, stream_len, &scene_data);
		
		if (result == cgltf_result_success)
		{
			cgltf_free(scene_data);
		}

		return;
	}
}