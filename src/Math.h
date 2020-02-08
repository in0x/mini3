#pragma once

#define MM_FORCEINL __forceinline
#define MM_VECTORCALL __vectorcall

// ====================================
//  Math Types
// ====================================

struct float2 { f32 x; f32 y; };
struct float3 { f32 x; f32 y; f32 z; };
struct float4 { f32 x; f32 y; f32 z; f32 w; };
struct float3x4 {  float4 rows[3]; };

// ====================================
//  Math Funcs
// ====================================

MM_FORCEINL float3 operator-(float3 f)
{
	return { -f.x, -f.y, -f.z };
}

MM_FORCEINL float3 operator+(float3 f)
{
	return f;
}

MM_FORCEINL float3 Vec3Zero()
{
	return float3{ 0.0f, 0.0f, 0.0f };
}

MM_FORCEINL float3 Vec3One()
{
	return float3{ 1.0f, 1.0f, 1.0f };
}

MM_FORCEINL float3 Vec3Make(float x, float y, float z)
{
	return float3{ x, y, z };
}

MM_FORCEINL float3 Vec3Add(float3 lhs, float3 rhs)
{
	return float3{ lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, };
}