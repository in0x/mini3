#pragma once

#include <mutex>
#include "Array.h"

struct KeyMsg
{
	s8 m_key;
	bool m_bKeyIsDown;
};

struct InputMessages
{
	InputMessages()
		: m_bWantsToQuit(false)
	{}

	Array<KeyMsg, 128> m_keys;
	bool m_bWantsToQuit;
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
	
	void AddKeyMessage(s8 key, bool bIsKeyDown)
	{
		ScopedLock lock(m_QueueLock);

		KeyMsg* msg = m_messages.m_keys.TryPushBack();

		if (msg)
		{
			msg->m_key = key;
			msg->m_bKeyIsDown = bIsKeyDown;
		}
		else
		{
			LOG(Log::Input, "Key input %d dropped because input buffer was full!", key);
		}
	}

	void AddQuitMessage()
	{
		ScopedLock lock(m_QueueLock);
		m_messages.m_bWantsToQuit = true;
	}

	virtual InputMessages PumpMessages() override
	{
		ScopedLock lock(m_QueueLock);
		
		InputMessages copy = m_messages;
		
		m_messages.m_keys.Clear();
		m_messages.m_bWantsToQuit = false;

		return copy;
	}

private:
	InputMessages m_messages;
	std::mutex m_QueueLock;
};