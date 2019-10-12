#include "Windows.h"

void LogLastWindowsError()
{
	LPTSTR errorText = nullptr;
	DWORD lastError = GetLastError();
	DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS;

	FormatMessage(
		flags,
		nullptr, // unused with FORMAT_MESSAGE_FROM_SYSTEM
		lastError,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&errorText,
		0,
		nullptr);

	if (errorText != nullptr)
	{
		LOG("%s", errorText);
		LocalFree(errorText);
	}
	else
	{
		LOG("Failed to get message for last windows error %u", static_cast<uint32_t>(lastError));
	}
}
