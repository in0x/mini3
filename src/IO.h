#pragma once
#include "Core.h"

namespace IO
{
	// Call to prepare the file system for I/O operations.
	void FileSysInit(char const* project_mount_path);
	void FileSysExit();
}