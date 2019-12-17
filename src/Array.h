#pragma once
#include "Core.h"
#include <type_traits>

template<typename T, u32 Capacity>
class Array
{
public:
	template<typename T>
	struct ConstIterator
	{
		T* elem_ptr;

		operator T const*()
		{
			return elem_ptr;
		}

		operator T const&()
		{
			return *elem_ptr;
		}

		bool operator ==(ConstIterator<T> const& other) const
		{
			return elem_ptr == other.elem_ptr;
		}

		bool operator !=(ConstIterator<T> const& other) const
		{
			return elem_ptr != other.elem_ptr;
		}

		ConstIterator<T>& operator++()
		{
			elem_ptr++;
			return *this;
		}
	};

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

	ConstIterator<T> begin() const
	{
		return { m_data };
	}

	ConstIterator<T> end() const
	{
		return { m_data + m_size };
	}

private:
	static_assert(std::is_pod<T>::value, "T must be pod!");

	T m_data[Capacity];
	u32 m_size;
};