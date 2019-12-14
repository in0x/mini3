#pragma once
#include "Core.h"

template<typename T, u32 Capacity>
class Array
{
public:
	Array() = default;

	Array(Array<T, Capacity> const& other)
	{
		memcpy(m_data, other.m_data, Capacity * sizeof(T));
	}

	void Clear()
	{
		m_size = 0;
	}

	T* PushBack()
	{
		ASSERT_F(m_size < Capacity, "Array exceeded capacity %u!", Capacity);
		return &m_data[m_size++];
	}

	T* TryPushBack()
	{
		if (m_size < Capacity)
		{
			return &m_data[m_size++];
		}
		else
		{
			return nullptr;
		}
	}

	void Size() const
	{
		return m_size;
	}

private:
	static_assert(std::is_pod<T>::value, "T must be pod!");

	T m_data[Capacity];
	u32 m_size;
};