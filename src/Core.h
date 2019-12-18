#pragma once

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <atomic>

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

//constexpr size_t S8_MAX

static constexpr size_t MAX_DEBUG_MSG_SIZE = 1024;
thread_local static char g_debugFmtBuffer[MAX_DEBUG_MSG_SIZE];
thread_local static char g_debugMsgBuffer[MAX_DEBUG_MSG_SIZE];

static constexpr size_t BytesToKiloBytes(size_t bytes)
{
	return bytes / (1024Ui64);
}

static constexpr size_t BytesToMegaBytes(size_t bytes)
{
	return bytes / (1024Ui64 * 1024Ui64);
}

static constexpr size_t BytesToGigaBytes(size_t bytes)
{
	return bytes / (1024Ui64 * 1024Ui64 * 1024Ui64);
}

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
		Input,

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
#define ASSERT_FAIL() assert(false)
#define ASSERT_FAIL_F(format, ...) ASSERT_F(false, format, __VA_ARGS__)
#define DEBUG_CODE(x) x
#else
#define ASSERT(x) 
#define ASSERT_F(x, format, ...)  
#define ASSERT_RESULT(hr)
#define ASSERT_RESULT_F(hr, format, ...)
#define ASSERT_FAIL()
#define ASSERT_FAIL_F(format, ...)
#define DEBUG_CODE(x)
#endif

#define UNUSED(x) (void)(x)

#define ARRAY_SIZE(x) _countof(x)

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

inline void memzero(void* dst, size_t size)
{
	memset(dst, 0, size);
}

inline ptrdiff_t GetAlignmentAdjustment(u8* raw, size_t alignment)
{
	uintptr_t ptr = reinterpret_cast<uintptr_t>(raw);

	size_t mask = (alignment - 1);
	uintptr_t misalignment = (ptr & mask);
	return alignment - misalignment;
}

inline u8* AlignAddress(u8* raw, size_t alignment)
{
	ptrdiff_t adjust = GetAlignmentAdjustment(raw, alignment);

	u8* aligned = raw + adjust;
	return aligned;
}