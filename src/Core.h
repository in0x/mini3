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
thread_local static char g_debugFmtBuffer[MAX_DEBUG_MSG_SIZE];
thread_local static char g_debugMsgBuffer[MAX_DEBUG_MSG_SIZE];

// Returns the position the null terminator was written to.
int MiniPrintf(char* buffer, size_t bufferLen, const char *fmt, bool appendNewline, ...);

struct Log
{
	enum Category
	{
		Default,
		Assert,
		GfxDevice,
		Win32,

		EnumCount,
		EnumFirst = Default,
	};

	static constexpr char* CategoryStrings[Category::EnumCount] = 
	{
		"Default",
		"ASSERT",
		"GfxDevice",
		"Win32",
	};
};

void DebugPrintf(char const* file, int line, char const* fmt, Log::Category category, ...);

#ifdef _DEBUG
#define LOG(category, format, ...) DebugPrintf(__FILE__, __LINE__, format, category, __VA_ARGS__); 
#else
#define LOG(format, ...)
#endif

#ifdef _DEBUG
#define ASSERT(x) assert(x)
#define ASSERT_F(x, format, ...) if (!(x)) { LOG(Log::Assert, format, __VA_ARGS__); assert(x); }
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