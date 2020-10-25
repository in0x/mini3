#include "FrameTimer.h"
#include "Win32.h"

FrameTimer::FrameTimer()
	: m_startedTime(0)
	, m_lastTickTime(0)
	, m_lastStopTime(0)
	, m_deltaTime(0.0)
	, m_secondsPerClock(0.0)
	, m_bIsStopped(false)
{

	u64 secondsPerClock;
	QueryPerformanceFrequency((LARGE_INTEGER*)&secondsPerClock);

	m_secondsPerClock = 1.0 / secondsPerClock;
}

static u64 QueryHWTimer()
{
	u64 time;
	QueryPerformanceCounter((LARGE_INTEGER*)&time);
	return time;
}

void ResetTimer(FrameTimer& timer)
{
	u64 currTime = QueryHWTimer();

	timer.m_startedTime = currTime;
	timer.m_lastTickTime = currTime;
	timer.m_lastStopTime = 0;
	timer.m_bIsStopped = false;
}

void StartTimer(FrameTimer& timer)
{
	if (!timer.m_bIsStopped)
	{
		return;
	}

	u64 startTime = QueryHWTimer();

	timer.m_timeSpentPaused += (startTime - timer.m_lastStopTime);
	timer.m_startedTime = startTime;
	timer.m_lastTickTime = startTime;
	timer.m_lastStopTime = 0;
	timer.m_bIsStopped = false;
}

void StopTimer(FrameTimer& timer)
{
	if (!timer.m_bIsStopped)
	{
		timer.m_lastStopTime = QueryHWTimer();
		timer.m_bIsStopped = true;
		timer.m_deltaTime = 0.0;
	}
}

void TickTimer(FrameTimer& timer)
{
	if (timer.m_bIsStopped)
	{
		timer.m_deltaTime = 0.0;
		return;
	}

	u64 currTime = QueryHWTimer();

	timer.m_deltaTime = (currTime - timer.m_lastTickTime) * timer.m_secondsPerClock;
	timer.m_deltaTime = max(0.0, timer.m_deltaTime);

	timer.m_lastTickTime = currTime;
}

f32 GetDeltaTimeS(FrameTimer& timer)
{
	return static_cast<f32>(timer.m_deltaTime);
}

f32 GetTotalTimeS(FrameTimer& timer)
{
	if (timer.m_bIsStopped)
	{
		return (timer.m_lastStopTime - timer.m_timeSpentPaused - timer.m_startedTime) * timer.m_secondsPerClock;
	}
	else
	{
		return (timer.m_lastTickTime - timer.m_timeSpentPaused - timer.m_startedTime) * timer.m_secondsPerClock;
	}
}