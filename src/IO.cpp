#include "IO.h"
#include "Win32.h"

namespace IO
{
	struct FileSys
	{
		char m_project_path[MAX_PATH];
	};

	static FileSys* s_file_sys;

	void FileSysInit(char const* project_mount_path)
	{
		// TODO(): Get this from a cvar
		//SetCurrentDirectory(project_mount_path);

		char cwd_buffer[MAX_PATH];
		GetCurrentDirectory(MAX_PATH, cwd_buffer);
		LOG(Log::IO, "Program working directory: %s", cwd_buffer);
		
		s_file_sys = new FileSys;
		strcpy_s(s_file_sys->m_project_path, project_mount_path);
	
		LOG(Log::IO, "Project mount path: %s", s_file_sys->m_project_path);
	}

	void FileSysExit()
	{
		delete s_file_sys;
	}
}