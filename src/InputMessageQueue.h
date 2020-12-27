#pragma once

#include <mutex>
#include "Array.h"

struct KeyCode
{
	enum Enum
	{
		A,
		D,
		W,
		S,
		ARROW_LEFT,
		ARROW_UP,
		ARROW_RIGHT,
		ARROW_DOWN,
		MSB_LEFT,
		MSB_MIDDLE,
		MSB_RIGHT,
		UNKNOWN,
		COUNT
	};
};

struct KeyMsg
{
	enum Type
	{
		KeyDown,
		KeyUp,
		MouseMove,
		MouseWheel,

		Count
	};

	KeyCode::Enum m_key; // Always set

	union
	{
		struct MousePos
		{
			f32 x; // Set when MouseMove
			f32 y; // Set when MouseMove
		} m_mouse_pos;

		f32 wheel_delta; // Set when MouseWheel

	} m_data;

	Type m_type;
};

struct InputMessages
{
	InputMessages()
		: m_wants_to_quit(false)
	{}

	Array<KeyMsg, 128> m_keys;
	bool m_wants_to_quit;
};

class IMessageQueueConsumer
{
public:
	// Pumps the message queue for its current state.
	virtual InputMessages PumpMessages() = 0;
};

// A thread safe input message queue intended for use with one producer
// thread and n consumer threads. Consumers acquire a safe copy of the
// current state of the queue, which also clears out the write buffer.
class ThreadSafeInputMessageQueue : public IMessageQueueConsumer
{
public:
	ThreadSafeInputMessageQueue()
	{}

	void AddKeyChange(KeyCode::Enum key, bool is_key_down)
	{
		ScopedLock lock(m_queue_lock);

		KeyMsg* msg = m_messages.m_keys.TryPushBack();

		if (msg)
		{
			msg->m_type = is_key_down ? KeyMsg::KeyDown : KeyMsg::KeyUp;
			msg->m_key = key;
		}
		else
		{
			// NOTE(): If this hits, its ringbuffer time.
			ASSERT_FAIL_F("Key input %d dropped because input buffer was full!");
		}
	}

	void AddMouseChange(KeyMsg::Type ev_type, KeyCode::Enum btn, f32 x, f32 y)
	{
		ScopedLock lock(m_queue_lock);

		KeyMsg* msg = m_messages.m_keys.TryPushBack();

		if (msg)
		{
			msg->m_key = btn;
			msg->m_type = ev_type;
			msg->m_data.m_mouse_pos.x = x;
			msg->m_data.m_mouse_pos.y = y;
		}
		else
		{
			ASSERT_FAIL_F("Key input %d dropped because input buffer was full!");
		}
	}

	void AddMouseWheelChange(f32 delta)
	{
		ScopedLock lock(m_queue_lock);

		KeyMsg* msg = m_messages.m_keys.TryPushBack();

		if (msg)
		{
			msg->m_key = KeyCode::MSB_MIDDLE;
			msg->m_type = KeyMsg::MouseWheel;
			msg->m_data.wheel_delta = delta;
		}
		else
		{
			ASSERT_FAIL_F("Key input %d dropped because input buffer was full!");
		}
	}

	void AddQuitMessage()
	{
		ScopedLock lock(m_queue_lock);
		m_messages.m_wants_to_quit = true;
	}

	virtual InputMessages PumpMessages() override
	{
		// TODO(): Would be nice to convert this to SRW lock.
		ScopedLock lock(m_queue_lock);
		
		InputMessages copy = m_messages;
		
		m_messages.m_keys.Clear(true);
		m_messages.m_wants_to_quit = false;

		return copy;
	}

private:
	InputMessages m_messages;
	std::mutex m_queue_lock;
};