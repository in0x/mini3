#pragma once
#include "Core.h"

namespace IO
{
	// Call to prepare the file system for I/O operations.
	void FileSysInit(char const* project_mount_path);
	void FileSysExit();

	static constexpr u64 s_max_path = 260;

	struct Path
	{
		char m_str[s_max_path] = "\0";
	};

	void GetAbsoluteFilePath(char const* rel_path, Path* out_abs_path);
}