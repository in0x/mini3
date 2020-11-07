#pragma once

#include <math.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>

typedef DirectX::XMMATRIX simdMatrix;
typedef DirectX::XMFLOAT4X3 mtx4x3;
typedef DirectX::XMFLOAT4X4 mtx4x4;
typedef DirectX::XMFLOAT2 vec2;
typedef DirectX::XMFLOAT3 vec3;
typedef DirectX::XMFLOAT4 vec4;

#define MM_INLINE inline
#define MM_FORCEINL __forceinline
#define MM_VECTORCALL __vectorcall
#define MM_DEFAULT_INL MM_INLINE

namespace Math
{
	// ====================================
	//  Math Types
	// ====================================

	//struct float2 { f32 x; f32 y; };
	//struct float3 { f32 x; f32 y; f32 z; };
	//struct float4 { f32 x; f32 y; f32 z; f32 w; };
	//struct float3x4 {  float4 rows[3]; };

	// ====================================
	//  Math Types Funcs
	// ====================================

	using namespace DirectX;

	constexpr f32 Pi = 3.1415926535f;

	static MM_DEFAULT_INL mtx4x4 MM_VECTORCALL MatrixTranslation(f32 x, f32 y, f32 z)
	{
		mtx4x4 out_mtx;
		XMStoreFloat4x4(&out_mtx, XMMatrixTranslation(x, y, z));
		return out_mtx;
	}

	static MM_DEFAULT_INL mtx4x4 MM_VECTORCALL MatrixRotationX(f32 angle_rad)
	{
		mtx4x4 out_mtx;
		XMStoreFloat4x4(&out_mtx, XMMatrixRotationX(angle_rad));
		return out_mtx;
	}

	static MM_DEFAULT_INL mtx4x4 MM_VECTORCALL MatrixRotationY(f32 angle_rad)
	{
		mtx4x4 out_mtx;
		XMStoreFloat4x4(&out_mtx, XMMatrixRotationY(angle_rad));
		return out_mtx;
	}

	static MM_DEFAULT_INL mtx4x4 MM_VECTORCALL MatrixScale(f32 x, f32 y, f32 z)
	{
		mtx4x4 out_mtx;
		XMStoreFloat4x4(&out_mtx, XMMatrixScaling(x, y, z));
		return out_mtx;
	}

	static MM_INLINE mtx4x4 MM_VECTORCALL MatrixTransform(vec3 translate, vec3 rotate, vec3 scale)
	{
		mtx4x4 out_mtx;

		XMStoreFloat4x4(
			&out_mtx,
			XMMatrixAffineTransformation(
				XMVectorSet(scale.x, scale.y, scale.z, 0.0f), 
				g_XMZero,
				XMVectorSet(rotate.x, rotate.y, rotate.z, 0.0f),
				XMVectorSet(translate.x, translate.y, translate.z, 0.0f)
		));

		return out_mtx;
	}

	static MM_DEFAULT_INL mtx4x4 MM_VECTORCALL MatrixMul(mtx4x4 const& a, mtx4x4 const& b)
	{
		XMMATRIX simd_a = XMLoadFloat4x4(&a);
		XMMATRIX simd_b = XMLoadFloat4x4(&b);
		
		mtx4x4 out_mtx;
		XMStoreFloat4x4(&out_mtx, XMMatrixMultiply(simd_a, simd_b));
		
		return out_mtx;
	}

	static MM_DEFAULT_INL mtx4x4 MM_VECTORCALL MatrixTranspose(mtx4x4 const& mtx)
	{
		XMMATRIX simd = XMLoadFloat4x4(&mtx);
		simd = XMMatrixTranspose(simd);
	
		mtx4x4 out_mtx;
		XMStoreFloat4x4(&out_mtx, simd);

		return out_mtx;
	}

	static MM_DEFAULT_INL mtx4x4 MM_VECTORCALL MatrixLookAtLH(vec3 eye_pos, vec3 look_at, vec3 up_dir)
	{
		XMMATRIX result = XMMatrixLookAtLH(
			XMVectorSet(eye_pos.x, eye_pos.y, eye_pos.z, 0.0f),
			XMVectorSet(look_at.x, look_at.y, look_at.z, 0.0f),
			XMVectorSet(up_dir.x, up_dir.y, up_dir.z, 0.0f)
		);

		mtx4x4 out_mtx;
		XMStoreFloat4x4(&out_mtx, result);

		return out_mtx;
	}

	static MM_DEFAULT_INL mtx4x4 MM_VECTORCALL MatrixPerspectiveFovLH(f32 fov_y_rad, f32 aspect_ratio, f32 near_z, f32 far_z)
	{
		XMMATRIX result = XMMatrixPerspectiveFovLH(fov_y_rad, aspect_ratio, near_z, far_z);

		mtx4x4 out_mtx;
		XMStoreFloat4x4(&out_mtx, result);
		
		return out_mtx;
	}

	// Returns a vec3 (0,0,0).
	static MM_DEFAULT_INL vec3 MM_VECTORCALL Vec3Zero()
	{
		static vec3 g_vec3_zero(0.0f, 0.0f, 0.0f);
		return g_vec3_zero;
	}

	// Returns a vec4 (0,0,0,0).
	static MM_DEFAULT_INL vec4 MM_VECTORCALL Vec4Zero()
	{
		static vec4 g_vec4_zero(0.0f, 0.0f, 0.0f, 0.0f);
		return g_vec4_zero;
	}

	// Returns a vec4 (0,0,0,1).
	static MM_DEFAULT_INL vec4 MM_VECTORCALL Vec4DefaultPos()
	{
		static vec4 g_vec4_default_pos(0.0f, 0.0f, 0.0f, 1.0f);
		return g_vec4_default_pos;
	}

	// Returns a vec4 (0,0,0,0).
	static MM_DEFAULT_INL vec4 MM_VECTORCALL Vec4DefaultDir()
	{
		return Vec4Zero();
	}

	static MM_DEFAULT_INL vec3 MM_VECTORCALL UpDir()
	{
		return vec3(0.0f, 1.0f, 0.0f);
	}

	// ====================================
	//  Math Utility Funcs
	// ====================================

	static MM_DEFAULT_INL f32 MM_VECTORCALL RadToDegree(f32 rad)
	{
		return rad * (180.0f / Pi);
	}

	static MM_DEFAULT_INL f32 MM_VECTORCALL DegreeToRad(f32 degree)
	{
		return degree * (Pi / 180.0f);
	}
}