#include "BaseApp.h"

BaseApp::BaseApp()
	: m_timer()
	, m_msgQueue(nullptr)
	, m_nativeHandle(nullptr)
	, m_bIsPaused(false)
{
}

void BaseApp::SetNativeHandle(void* nativeHandle)
{
	m_nativeHandle = nativeHandle;
}

void* BaseApp::GetNativeHandle()
{
	return m_nativeHandle;
}

void BaseApp::SetMessageQueue(IMessageQueueConsumer* queue)
{
	m_msgQueue = queue;
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
	outAvgFps = m_frameStats.avgFps;
	outAvgMsPerFrame = m_frameStats.avgMs;

	bool statsAreNew = m_frameStats.m_bStatsRefreshed;
	m_frameStats.m_bStatsRefreshed = false;
	return statsAreNew;
}

void BaseApp::UpdateFrameStats()
{
	m_frameStats.counter++;

	// Sample every second.
	f32 totalTime = GetTotalTimeS(m_timer);
	f32 elapsedTime = totalTime - m_frameStats.elapsedTime;
	if (elapsedTime >= 1.0f)
	{
		m_frameStats.avgFps = m_frameStats.counter;
		m_frameStats.avgMs = 1000.0f / m_frameStats.counter;

		m_frameStats.counter = 0.0f;
		m_frameStats.elapsedTime += 1.0f;

		m_frameStats.m_bStatsRefreshed = true;
	}
}

void BaseApp::Exit()
{
	StopTimer(m_timer);
}