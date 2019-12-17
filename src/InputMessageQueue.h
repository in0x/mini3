#pragma once

#include <mutex>
#include "Array.h"

struct KeyMsg
{
	s8 m_key;
	bool m_is_key_down;
};

struct InputMessages
{
	InputMessages()
		: m_wants_to_quit(false)
	{}

	Array<KeyMsg, 128> m_keys;
	bool m_wants_to_quit;
};

using ScopedLock = std::lock_guard<std::mutex>;

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
	
	void AddKeyMessage(s8 key, bool is_key_down)
	{
		ScopedLock lock(m_queue_lock);

		KeyMsg* msg = m_messages.m_keys.TryPushBack();

		if (msg)
		{
			msg->m_key = key;
			msg->m_is_key_down = is_key_down;
		}
		else
		{
			LOG(Log::Input, "Key input %d dropped because input buffer was full!", key);
		}
	}

	void AddQuitMessage()
	{
		ScopedLock lock(m_queue_lock);
		m_messages.m_wants_to_quit = true;
	}

	virtual InputMessages PumpMessages() override
	{
		ScopedLock lock(m_queue_lock);
		
		InputMessages copy = m_messages;
		
		m_messages.m_keys.Clear();
		m_messages.m_wants_to_quit = false;

		return copy;
	}

private:
	InputMessages m_messages;
	std::mutex m_queue_lock;
};