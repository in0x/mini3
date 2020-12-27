#pragma once

#include "Core.h"
#include <math.h>

#define MM_INLINE inline
#define MM_FORCEINL __forceinline
#define MM_VECTORCALL __vectorcall
#define MM_DEFAULT_INL MM_INLINE

// ====================================
//  Math Types
//  Notes:
//  *) Uses column major convention
// ====================================

#pragma warning(push)
#pragma warning(disable:4201) // warning C4201: nonstandard extension used : nameless struct/union

struct vec2
{
	union
	{
		f32 data[2];
		struct { f32 x; f32 y; };
	};

	vec2() = default;

	vec2(f32 _x, f32 _y)
		: x(_x), y(_y)
	{}
};

struct vec3
{
	union
	{
		f32 data[3];
		struct { f32 x; f32 y; f32 z; };
	};

	vec3() = default;

	vec3(f32 _x, f32 _y, f32 _z)
		: x(_x), y(_y), z(_z)
	{}
};

struct vec4
{
	union
	{
		f32 data[4];
		struct { f32 x; f32 y; f32 z; f32 w; };
		vec3 xyz;
	};

	vec4() = default;

	vec4(f32 _x, f32 _y, f32 _z, f32 _w)
		: x(_x), y(_y), z(_z), w(_w)
	{}

	vec4(vec3 const& v)
	{
		xyz = v;
		w = 0.0f;
	}
};

struct mat44
{
	union
	{
		f32 data[16]; // NOTE(): column major

		// NOTE(): If we want to preserve (row, column) addressing
		// we can define a element by element struct here with out
		// of order naming, such that the user can just access e.g.
		// m21 that is at the correct memory location.
	};

	mat44() = default;

	mat44(
		f32 m00, f32 m01, f32 m02, f32 m03,
		f32 m10, f32 m11, f32 m12, f32 m13,
		f32 m20, f32 m21, f32 m22, f32 m23,
		f32 m30, f32 m31, f32 m32, f32 m33)
	{
		data[0] = m00;
		data[1] = m10;
		data[2] = m20;
		data[3] = m30;

		data[4] = m01;
		data[5] = m11;
		data[6] = m21;
		data[7] = m31;

		data[8] = m02;
		data[9] = m12;
		data[10] = m22;
		data[11] = m32;

		data[12] = m03;
		data[13] = m13;
		data[14] = m23;
		data[15] = m33;
	}

	MM_DEFAULT_INL f32& operator()(u32 row, u32 col)
	{
		ASSERT(row < 4 && col < 4);
		return data[col * 4 + row];
	}

	MM_DEFAULT_INL f32 const& operator()(u32 row, u32 col) const
	{
		ASSERT(row < 4 && col < 4);
		return data[col * 4 + row];
	}

	static mat44 const& Identity()
	{
		static const mat44 s_identity = mat44(
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		);

		return s_identity;
	}
};

#pragma warning(pop)

namespace Math
{
	// ====================================
	//  Constants
	// ====================================

	constexpr f32 Pi = 3.1415926535f;

	// ====================================
	//  Math Utility Funcs
	// ====================================

	constexpr static MM_DEFAULT_INL f32 MM_VECTORCALL RadToDegree(f32 rad)
	{
		return rad * (180.0f / Pi);
	}

	constexpr static MM_DEFAULT_INL f32 MM_VECTORCALL DegreeToRad(f32 degree)
	{
		return degree * (Pi / 180.0f);
	}

	struct Rad;

	struct Deg
	{
		f32 m_value;

		constexpr explicit Deg() : m_value(0.0f) {}
		constexpr explicit Deg(f32 value)
			: m_value(value)
		{}
	};

	struct Rad
	{
		f32 m_value;

		constexpr explicit Rad() : m_value(0.0f) {}
		
		constexpr explicit Rad(f32 value)
			: m_value(value)
		{}

		constexpr explicit Rad(Deg degrees)
			: m_value(DegreeToRad(degrees.m_value))
		{}

		constexpr operator Deg() const 
		{
			return Deg(RadToDegree(m_value));
		}
	};

	namespace Test
	{
		void Run();
	}

	// ====================================
	//  Math Types Funcs
	// ====================================

	static MM_DEFAULT_INL vec3 MM_VECTORCALL operator-(vec3 a, vec3 b)
	{
		return vec3(a.x - b.x, a.y - b.y, a.z - b.z);
	}

	static MM_DEFAULT_INL vec3 MM_VECTORCALL operator-(vec3 a)
	{
		return vec3(-a.x, -a.y, -a.z);
	}

	static MM_DEFAULT_INL f32 MM_VECTORCALL Length(vec3 v)
	{
		return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
	}

	static MM_DEFAULT_INL vec3 MM_VECTORCALL Normalize(vec3 v)
	{
		f32 len = Length(v);
		return vec3( 
			v.x / len,
			v.y / len,
			v.z / len);
	}

	static MM_DEFAULT_INL f32 MM_VECTORCALL Dot(vec3 a, vec3 b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}
	
	static MM_DEFAULT_INL vec3 MM_VECTORCALL Cross(vec3 a, vec3 b)
	{
		return vec3(
			a.y * b.z - a.z * b.y,
			a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x);
	}

	template <typename Matrix>
	static MM_DEFAULT_INL Matrix MM_VECTORCALL Translation(f32 x, f32 y, f32 z)
	{
		Matrix mat = Matrix::Identity();
		mat(0, 3) = x;
		mat(1, 3) = y;
		mat(2, 3) = z;

		return mat;
	}

	template <typename Matrix>
	static MM_DEFAULT_INL Matrix MM_VECTORCALL RotationX(Rad angle_rad)
	{
		Matrix mat = Matrix::Identity();

		f32 const c = cosf(angle_rad.m_value);
		f32 const s = sinf(angle_rad.m_value);

		mat(1, 1) = c;
		mat(2, 1) = s;
		mat(1, 2) = -s;
		mat(2, 2) = c;
	
		return mat;
	}

	//static MM_DEFAULT_INL mtx4x4 MM_VECTORCALL MatrixRotationY(f32 angle_rad)
	//{
	//	mtx4x4 out_mtx;
	//	XMStoreFloat4x4(&out_mtx, XMMatrixRotationY(angle_rad));
	//	return out_mtx;
	//}

	template <typename Matrix>
	static MM_DEFAULT_INL Matrix MM_VECTORCALL RotationXYZ(Rad x_rad, Rad y_rad, Rad z_rad)
	{
		f32 const A = cosf(x_rad.m_value);
		f32 const B = sinf(x_rad.m_value);
		f32 const C = cosf(y_rad.m_value);
		f32 const D = sinf(y_rad.m_value);
		f32 const E = cosf(z_rad.m_value);
		f32 const F = sinf(z_rad.m_value);

		return mat44(
			C * E,             -C * F,             -D,     0.0f,
			-B * D * E + A * F, B * D * F + A * E, -B * C, 0.0f,
			A * D * E + B * F, -A * D * F + B * E, A * C,  0.0f,
			0.0f,              0.0f,               0.0f,   1.0f
		);
	}

	template <typename Matrix>
	static MM_FORCEINL Matrix MM_VECTORCALL RotationXYZ(vec3 euler_angles)
	{
		return RotationXYZ(DegreeToRad(euler_angles.x), DegreeToRad(euler_angles.y), DegreeToRad(euler_angles.z));
	}


	static MM_DEFAULT_INL mat44 MM_VECTORCALL Mul(mat44 const& a, mat44 const& b)
	{
		mat44 dst;

#if 1
		dst(0, 0) = a(0, 0) * b(0, 0) + a(0, 1) * b(1, 0) + a(0, 2) * b(2, 0) + a(0, 3) * b(3, 0);
		dst(0, 1) = a(0, 0) * b(0, 1) + a(0, 1) * b(1, 1) + a(0, 2) * b(2, 1) + a(0, 3) * b(3, 1);
		dst(0, 2) = a(0, 0) * b(0, 2) + a(0, 1) * b(1, 2) + a(0, 2) * b(2, 2) + a(0, 3) * b(3, 2);
		dst(0, 3) = a(0, 0) * b(0, 3) + a(0, 1) * b(1, 3) + a(0, 2) * b(2, 3) + a(0, 3) * b(3, 3);

		dst(1, 0) = a(1, 0) * b(0, 0) + a(1, 1) * b(1, 0) + a(1, 2) * b(2, 0) + a(1, 3) * b(3, 0);
		dst(1, 1) = a(1, 0) * b(0, 1) + a(1, 1) * b(1, 1) + a(1, 2) * b(2, 1) + a(1, 3) * b(3, 1);
		dst(1, 2) = a(1, 0) * b(0, 2) + a(1, 1) * b(1, 2) + a(1, 2) * b(2, 2) + a(1, 3) * b(3, 2);
		dst(1, 3) = a(1, 0) * b(0, 3) + a(1, 1) * b(1, 3) + a(1, 2) * b(2, 3) + a(1, 3) * b(3, 3);

		dst(2, 0) = a(2, 0) * b(0, 0) + a(2, 1) * b(1, 0) + a(2, 2) * b(2, 0) + a(2, 3) * b(3, 0);
		dst(2, 1) = a(2, 0) * b(0, 1) + a(2, 1) * b(1, 1) + a(2, 2) * b(2, 1) + a(2, 3) * b(3, 1);
		dst(2, 2) = a(2, 0) * b(0, 2) + a(2, 1) * b(1, 2) + a(2, 2) * b(2, 2) + a(2, 3) * b(3, 2);
		dst(2, 3) = a(2, 0) * b(0, 3) + a(2, 1) * b(1, 3) + a(2, 2) * b(2, 3) + a(2, 3) * b(3, 3);

		dst(3, 0) = a(3, 0) * b(0, 0) + a(3, 1) * b(1, 0) + a(3, 2) * b(2, 0) + a(3, 3) * b(3, 0);
		dst(3, 1) = a(3, 0) * b(0, 1) + a(3, 1) * b(1, 1) + a(3, 2) * b(2, 1) + a(3, 3) * b(3, 1);
		dst(3, 2) = a(3, 0) * b(0, 2) + a(3, 1) * b(1, 2) + a(3, 2) * b(2, 2) + a(3, 3) * b(3, 2);
		dst(3, 3) = a(3, 0) * b(0, 3) + a(3, 1) * b(1, 3) + a(3, 2) * b(2, 3) + a(3, 3) * b(3, 3);
#else
		for (int row = 0; row < 4; row++)
		{
			for (int col = 0; col < 4; col++)
			{
				dst(row, col) = a(row, 0) * b(0, col) + a(row, 1) * b(1, col) + a(row, 2) * b(2, col) + a(row, 3) * b(3, col);
			}
		}
#endif

		return dst;
	}

	static MM_DEFAULT_INL vec4 MM_VECTORCALL Mul(mat44 const& mat, vec4 const& vec)
	{
		return vec4(
			mat(0, 0) * vec.x + mat(0, 1) * vec.y + mat(0, 2) * vec.z + mat(0, 3) * vec.w,
			mat(1, 0) * vec.x + mat(1, 1) * vec.y + mat(1, 2) * vec.z + mat(1, 3) * vec.w,
			mat(2, 0) * vec.x + mat(2, 1) * vec.y + mat(2, 2) * vec.z + mat(2, 3) * vec.w,
			mat(3, 0) * vec.x + mat(3, 1) * vec.y + mat(3, 2) * vec.z + mat(3, 3) * vec.w);
	}

	static MM_DEFAULT_INL vec4 MM_VECTORCALL Mul(vec4 const& vec, f32 scalar)
	{
		return vec4(
			vec.x * scalar,
			vec.y * scalar,
			vec.z * scalar,
			vec.w * scalar
		);
	}

	static MM_DEFAULT_INL bool MM_VECTORCALL Cmp(mat44 const& a, mat44 const& b)
	{
		 return memcmp(a.data, b.data, 16 * sizeof(f32)) == 0;
	}

	static MM_DEFAULT_INL mat44 MM_VECTORCALL Transpose(mat44 const& mat)
	{
		return mat44(
			mat(0, 0), mat(1, 0), mat(2, 0), mat(3, 0),
			mat(0, 1), mat(1, 1), mat(2, 1), mat(3, 1),
			mat(0, 2), mat(1, 2), mat(2, 2), mat(3, 2),
			mat(0, 3), mat(1, 3), mat(2, 3), mat(3, 3));
	}

	static MM_DEFAULT_INL mat44 MM_VECTORCALL MatrixLookAtLH(vec3 eye_pos, vec3 look_at, vec3 up_dir)
	{
		vec3 z_axis = Normalize((look_at - eye_pos));
		
		vec3 x_axis = Normalize(Cross(up_dir, z_axis));

		vec3 y_axis = Cross(z_axis, x_axis);

		return mat44(
			x_axis.x, x_axis.y, x_axis.z, -Dot(x_axis, eye_pos),
			y_axis.x, y_axis.y, y_axis.z, -Dot(y_axis, eye_pos),
			z_axis.x, z_axis.y, z_axis.z, -Dot(z_axis, eye_pos),
			0,        0,        0,        1
		);
	}

	static MM_DEFAULT_INL mat44 MM_VECTORCALL MatrixPerspectiveFovLH(f32 fov_y_rad, f32 aspect_ratio, f32 near_z, f32 far_z)
	{
		f32 g = 1.0f / tanf(fov_y_rad * 0.5f);
		f32 k = far_z / (far_z - near_z);
	
		return mat44(
			g / aspect_ratio, 0, 0, 0,
			0, g, 0, 0,
			0, 0, k, -near_z * k,
			0, 0, 1.0f, 0);
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
}

static MM_FORCEINL mat44 MM_VECTORCALL operator*(mat44 const& a, mat44 const& b)
{
	return Math::Mul(a, b);
}

static MM_FORCEINL vec4 MM_VECTORCALL operator*(vec4 const& vec, f32 scalar)
{
	return Math::Mul(vec, scalar);
}

static MM_FORCEINL bool MM_VECTORCALL operator==(mat44 const& a, mat44 const& b)
{
	return Math::Cmp(a, b);
}