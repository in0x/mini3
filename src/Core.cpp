#include "Core.h"
#include "Win32.h"
#include <time.h>

int MiniPrintfVA(char* buffer, size_t bufferLen, const char *fmt, bool appendNewline, va_list vl)
{
	int lastWritePos = vsnprintf_s(buffer, bufferLen, _TRUNCATE, fmt, vl);

	int const charsNeededForPostFix = appendNewline ? 2 : 1;
	bool bEnoughSpaceForPostFix = (bufferLen - lastWritePos) >= charsNeededForPostFix;

	if (lastWritePos < 0 || lastWritePos == bufferLen || !bEnoughSpaceForPostFix)
	{
		if (appendNewline)
		{
			buffer[bufferLen - 2] = '\n';
		}

		buffer[bufferLen - 1] = '\0';
		return lastWritePos;
	}
	else
	{
		if (appendNewline)
		{
			buffer[lastWritePos++] = '\n';
		}

		buffer[lastWritePos] = '\0';
		return lastWritePos;
	}
}

int MiniPrintf(char* buffer, size_t bufferLen, const char *fmt, bool appendNewline, ...)
{
	va_list vl;

	va_start(vl, appendNewline);
	int ret = MiniPrintfVA(buffer, bufferLen, fmt, appendNewline, vl);
	va_end(vl);

	return ret;
}

void DebugPrintf(char const* file, int line, char const* fmt, Log::Category category, ...)
{	
	memset(g_debugFmtBuffer, 0, MAX_DEBUG_MSG_SIZE);
	memset(g_debugMsgBuffer, 0, MAX_DEBUG_MSG_SIZE);

	size_t charsAvailable = MAX_DEBUG_MSG_SIZE;
	size_t lastWritePos = 0;
	
	lastWritePos = MiniPrintf(g_debugFmtBuffer + lastWritePos, charsAvailable, "%s(%d): ", false, file, line);
	charsAvailable = MAX_DEBUG_MSG_SIZE - lastWritePos;

	time_t rawtime;
	time(&rawtime);
	tm timeinfo;
	localtime_s(&timeinfo, &rawtime);

	lastWritePos += strftime(g_debugFmtBuffer + lastWritePos, charsAvailable, "[%T]", &timeinfo);
	charsAvailable = MAX_DEBUG_MSG_SIZE - lastWritePos;
	
	lastWritePos += MiniPrintf(g_debugFmtBuffer + lastWritePos, charsAvailable, " [%s]: ", false, Log::CategoryStrings[category]);
	charsAvailable = MAX_DEBUG_MSG_SIZE - lastWritePos;

	va_list vl;
	
	va_start(vl, category);
	MiniPrintfVA(g_debugMsgBuffer, MAX_DEBUG_MSG_SIZE, fmt, true, vl);
	va_end(vl);

	strcat_s(g_debugFmtBuffer, charsAvailable, g_debugMsgBuffer);

	OutputDebugString(g_debugFmtBuffer);
}