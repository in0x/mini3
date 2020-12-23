#include "Win32.h"

void LogWindowsError(u32 last_error)
{
	if (last_error == ERROR_SUCCESS)
	{
		return;
	}

	LPTSTR error_text = nullptr;
	DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS;

	FormatMessage(
		flags,
		nullptr, // unused with FORMAT_MESSAGE_FROM_SYSTEM
		last_error,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&error_text,
		0,
		nullptr);

	if (error_text != nullptr)
	{
		LOG(Log::Win32, "%s", error_text);
		LocalFree(error_text);
	}
	else
	{
		LOG(Log::Win32, "Failed to get message for last windows error %u", static_cast<u32>(last_error));
	}
}

void LogLastWindowsError()
{
	LogWindowsError(GetLastError());
}

static u64 s_large_page_size = 0;
static u64 s_small_page_size = 0;

bool TryEnableLargePages()
{
	if (s_large_page_size > 0) return true;

	bool success = false;
	ON_SCOPE_EXIT(if (success == false) 
	{
		LOG(Log::Win32, "Failed to enable large page support.");
		LogLastWindowsError();
	});

	// See: https://devblogs.microsoft.com/oldnewthing/20110128-00/?p=11643
	HANDLE token;
	success = OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token);
	if (success == false)
	{
		return false;
	}
	
	TOKEN_PRIVILEGES priviliges;
	success = LookupPrivilegeValue(nullptr, TEXT("SeLockMemoryPrivilege"), &priviliges.Privileges[0].Luid);
	if (success == false)
	{
		return false;
	}

	priviliges.PrivilegeCount = 1;
	priviliges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	success = AdjustTokenPrivileges(token, false, &priviliges, 0, nullptr, 0);
	if (success == false)
	{
		return false;
	}

	u32 last_error = GetLastError();
	success = (last_error == ERROR_SUCCESS);
	if (success == false)
	{
		LOG(Log::Win32, "Failed setting large page privilige with error: ");
		LogWindowsError(last_error);
		return false;
	}

	s_large_page_size = GetLargePageMinimum();

	return true;
}

u64 QuerySmallPageSize()
{
	if (s_small_page_size == 0)
	{
		_SYSTEM_INFO info; GetSystemInfo(&info);
		s_small_page_size = info.dwPageSize;
	}

	return s_small_page_size;

}

u64 QueryLargePageSize()
{
	return s_large_page_size;
}