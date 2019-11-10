#pragma once

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>

typedef uint8_t   u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef float f32;
typedef double f64;

static constexpr size_t MAX_DEBUG_MSG_SIZE = 1024;
static char g_debugFmtBuffer[MAX_DEBUG_MSG_SIZE];
static char g_debugMsgBuffer[MAX_DEBUG_MSG_SIZE];

inline int miniFmtDebugMsg(char* buffer, size_t bufferLen, const char *fmt, ...)
{
	va_list ap;
	int retval;

	va_start(ap, fmt);
	retval = sprintf_s(buffer, bufferLen, fmt, ap);
	va_end(ap);

	if (retval < bufferLen)
	{
		buffer[retval + 1] = '\n';
		buffer[retval + 2] = '\0';
	}
	else
	{
		buffer[bufferLen - 2] = '\n';
		buffer[bufferLen - 1] = '\0';
	}

	return retval;
}

void DebugPrintf(char const* buffer);

#ifdef _DEBUG
#define LOG(format, ...) _snprintf_s(g_debugMsgBuffer, MAX_DEBUG_MSG_SIZE, format, __VA_ARGS__); DebugPrintf(g_debugMsgBuffer); 
#else
#define LOG(format, ...)
#endif

#ifdef _DEBUG
#define ASSERT(x) assert(x)
#define ASSERT_F(x, format, ...) if (!(x)) { LOG(format, __VA_ARGS__); assert(x); }
#define ASSERT_RESULT(hr) assert(SUCCEEDED(hr))
#define ASSERT_RESULT_F(hr, format, ...) ASSERT_F(SUCCEEDED(hr), format, __VA_ARGS__)
#else
#define ASSERT(x) 
#define ASSERT_F(x, format, ...)  
#define ASSERT_RESULT(hr)
#define ASSERT_RESULT_F(hr, format, ...)
#endif

#define UNUSED(x) (void)(x)

template <class T>
T max(const T& a, const T& b)
{
	return (b > a) ? b : a;
}

template <class T>
T min(const T& a, const T& b)
{
	return (b < a) ? b : a;
}