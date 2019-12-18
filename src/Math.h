#pragma once

#define MM_FORCEINL __forceinline
#define MM_VECTORCALL __vectorcall

// ====================================
//  Math Types
// ====================================

struct Vec3
{
	float x;
	float y;
	float z;
};

struct Vec4
{
	float x;
	float y;
	float z;
	float w;
};

struct Mtx34
{
	float row_column[3][4];
};

// ====================================
//  Math Funcs
// ====================================

MM_FORCEINL Vec3 Vec3Zero()
{
	return Vec3{ 0.0f, 0.0f, 0.0f };
}

MM_FORCEINL Vec3 Vec3One()
{
	return Vec3{ 1.0f, 1.0f, 1.0f };
}

MM_FORCEINL Vec3 Vec3Make(float x, float y, float z)
{
	return Vec3{ x, y, z };
}

MM_FORCEINL Vec3 Vec3Add(Vec3 lhs, Vec3 rhs)
{
	return Vec3{ lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, };
}