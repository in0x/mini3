#include "Core.h"
#include "Win32.h"

void DebugPrintf(char const* buffer)
{
	OutputDebugString(buffer);
}