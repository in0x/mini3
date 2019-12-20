#pragma once
#include "Core.h"
#include <type_traits>

struct BasicCounterPolicy
{
	typedef u32 CounterType;
};

struct AtomicCounterPolicy
{
	typedef std::atomic<u32> CounterType;
};

template<typename T, u32 Capacity, typename CounterPolicy = BasicCounterPolicy>
class Array
{
public:
	template<typename T>
	struct ConstIterator
	{
		ConstIterator(T const* ptr) : elem_ptr(ptr) {}

		T const* elem_ptr;

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

	void Reserve(u32 count)
	{
		ASSERT(m_size + count <= Capacity);
		m_size += count;
	}

	T* Data()
	{
		return m_data;
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

	void PushBack(T value)
	{
		ASSERT_F(m_size < Capacity, "Array exceeded capacity %u!", Capacity);
		m_data[m_size++] = value;
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

	void PopBack()
	{
		m_size--;
	}

	u32 Size() const
	{
		return m_size;
	}

	T& operator[](u32 index)
	{
		ASSERT(index < m_size);
		return m_data[index];
	}

	ConstIterator<T> begin() const
	{
		return ConstIterator<T>( m_data );
	}

	ConstIterator<T> end() const
	{
		return ConstIterator<T>( &m_data[m_size] );
	}

private:
	static_assert(std::is_trivially_copyable<T>::value, "T must be pod-like type!");
	typedef typename CounterPolicy::CounterType CounterType;

	T m_data[Capacity];
	CounterType m_size;
};