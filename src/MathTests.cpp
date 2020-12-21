#include "Math.h"

namespace Math
{
namespace Test
{
	void Mat44ConstructFromElements()
	{
		mat44 increasing = mat44(
			1, 2, 3, 4,
			5, 6, 7, 8,
			9, 10, 11, 12,
			13, 14, 15, 16
		);

		f32 cpm_data[16] = 
		{
			1, 5, 9, 13,
			2, 6, 10, 14,
			3, 7, 11, 15,
			4, 8, 12, 16
		};

		for (u32 i = 0; i < 16; ++i)
		{
			ASSERT(increasing.data[i] == cpm_data[i]);
		}
	}

	void Mat44IndexAccess()
	{
		mat44 increasing = mat44(
			1, 2, 3, 4,
			5, 6, 7, 8,
			9, 10, 11, 12,
			13, 14, 15, 16
		);

		ASSERT(increasing(0, 0) == 1);
		ASSERT(increasing(0, 1) == 2);
		ASSERT(increasing(0, 2) == 3);
		ASSERT(increasing(0, 3) == 4);
		
		ASSERT(increasing(1, 0) == 5);
		ASSERT(increasing(1, 1) == 6);
		ASSERT(increasing(1, 2) == 7);
		ASSERT(increasing(1, 3) == 8);
		
		ASSERT(increasing(2, 0) == 9);
		ASSERT(increasing(2, 1) == 10);
		ASSERT(increasing(2, 2) == 11);
		ASSERT(increasing(2, 3) == 12);
		
		ASSERT(increasing(3, 0) == 13);
		ASSERT(increasing(3, 1) == 14);
		ASSERT(increasing(3, 2) == 15);
		ASSERT(increasing(3, 3) == 16);
	}

	void Mat44Cmp()
	{
		mat44 identity_a = mat44::Identity();
		mat44 identity_b = mat44::Identity();

		ASSERT(Math::Cmp(identity_a, identity_b));
	}

	void Mat44Mul()
	{
		mat44 identity_a = mat44::Identity();
		mat44 identity_b = mat44::Identity();
	
		mat44 identity_c = identity_a * identity_b;

		ASSERT(identity_c == identity_a);

		mat44 mat_a( 
			1, 4, 2, 3,
			2, 5, 2, 1,
			2, 5, 8, 1,
			9, 1, 4, 7);

		mat44 mat_b(
			0, 1, 2, 0,
			5, 7, 0, 9,
			3, 4, 2, 6,
			1, 9, 7, 6);

		mat44 mat_expected(
			29, 64, 27, 66,
			32, 54, 15, 63,
			50, 78, 27, 99,
			24, 95, 75, 75);

		mat44 mat_actual = mat_a * mat_b;

		ASSERT(mat_expected == mat_actual);
	}

	void RadDegreeConverions()
	{
		{
			Deg deg_90(90.0f);
			Rad rad_90(deg_90);

			ASSERT(NearlyEqual(rad_90.m_value, Math::Pi / 2.0f));
		}
		{
			Deg deg_180(180.0f);
			Rad rad_180(deg_180);

			ASSERT(NearlyEqual(rad_180.m_value, Math::Pi));
		}
		{
			Deg deg_neg45(-45.0f);
			Rad rad_neg45(deg_neg45);

			ASSERT(NearlyEqual(rad_neg45.m_value, -Math::Pi / 4.0f));
		}
	}

	void Run()
	{
		Mat44ConstructFromElements();
		Mat44IndexAccess();
		Mat44Cmp();
		Mat44Mul();
		RadDegreeConverions();
	}
}
}