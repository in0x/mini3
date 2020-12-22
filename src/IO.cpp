#include "IO.h"
#include "Win32.h"

namespace IO
{
	struct FileSys
	{
		char m_project_path[s_max_path]; // Project file root, without path delimiter.
		u64 m_project_path_len;
	};

	static FileSys s_file_sys;

	void FileSysInit(char const* project_mount_path)
	{
		// TODO(): Get this from a cvar
		//SetCurrentDirectory(project_mount_path);

		char cwd_buffer[s_max_path];
		GetCurrentDirectory(s_max_path, cwd_buffer);
		LOG(Log::IO, "Program working directory: %s", cwd_buffer);
		
		strcpy_s(s_file_sys.m_project_path, project_mount_path);
	
		// Strip delimiter if present for consistency.
		char* project_path = s_file_sys.m_project_path;
		u64 path_len = strlen(project_path);
		u64 end_idx = path_len - 1;
		if ((project_path[end_idx] == '\\') ||
			(project_path[end_idx] == '/'))
		{
			project_path[end_idx] = '\0';
			path_len--;
		}

		s_file_sys.m_project_path_len = path_len;
	
		LOG(Log::IO, "Project mount path: %s", s_file_sys.m_project_path);
	}

	void FileSysExit()
	{
	}

	void GetAbsoluteFilePath(char const* rel_path, Path* out_abs_path)
	{
		bool rel_path_is_term = ((rel_path[0] == '\\') || (rel_path[1] == '/'));
		char const* fmt_str = rel_path_is_term ? "%s%s" : "%s\\%s";
		
		MiniPrintf(out_abs_path->m_str, s_max_path, fmt_str, false, s_file_sys.m_project_path, rel_path);
	}
}