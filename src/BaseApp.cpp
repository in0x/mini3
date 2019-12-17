#include "BaseApp.h"

BaseApp::BaseApp()
	: m_timer()
	, m_msg_queue(nullptr)
	, m_native_handle(nullptr)
	, m_b_is_paused(false)
{
}

void BaseApp::SetNativeHandle(void* nativeHandle)
{
	m_native_handle = nativeHandle;
}

void* BaseApp::GetNativeHandle()
{
	return m_native_handle;
}

void BaseApp::SetMessageQueue(IMessageQueueConsumer* queue)
{
	m_msg_queue = queue;
}

void BaseApp::Init()
{
	ResetTimer(m_timer);
}

bool BaseApp::Update()
{
	TickTimer(m_timer);
	UpdateFrameStats();

	return true;
}

bool BaseApp::GetStats(f32& outAvgFps, f32& outAvgMsPerFrame)
{
	outAvgFps = m_frameStats.avg_fps;
	outAvgMsPerFrame = m_frameStats.avg_ms;

	bool statsAreNew = m_frameStats.m_b_stats_refreshed;
	m_frameStats.m_b_stats_refreshed = false;
	return statsAreNew;
}

void BaseApp::UpdateFrameStats()
{
	m_frameStats.counter++;

	// Sample every second.
	f32 totalTime = GetTotalTimeS(m_timer);
	f32 elapsedTime = totalTime - m_frameStats.elapsed_time;
	if (elapsedTime >= 1.0f)
	{
		m_frameStats.avg_fps = m_frameStats.counter;
		m_frameStats.avg_ms = 1000.0f / m_frameStats.counter;

		m_frameStats.counter = 0.0f;
		m_frameStats.elapsed_time += 1.0f;

		m_frameStats.m_b_stats_refreshed = true;
	}
}

void BaseApp::Exit()
{
	StopTimer(m_timer);
}