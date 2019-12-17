#pragma once

#define MINI_MATH_FORCEINL

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

MINI_MATH_FORCEINL Vec3 Vec3Zero()
{
	return Vec3{ 0.0f, 0.0f, 0.0f };
}

MINI_MATH_FORCEINL Vec3 Vec3One()
{
	return Vec3{ 1.0f, 1.0f, 1.0f };
}

MINI_MATH_FORCEINL Vec3 Vec3Make(float x, float y, float z)
{
	return Vec3{ x, y, z };
}

MINI_MATH_FORCEINL Vec3 Vec3Add(Vec3 lhs, Vec3 rhs)
{
	return Vec3{ rhs.x + rhs.x, rhs.y + rhs.y, rhs.z + rhs.z, };
}