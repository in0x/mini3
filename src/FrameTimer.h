#pragma once
#include "Core.h"

struct FrameTimer
{
	FrameTimer();

	u64 m_startedTime;
	u64 m_timeSpentPaused;
	u64 m_lastStopTime;
	u64 m_lastTickTime;

	f64 m_deltaTime;
	f64 m_secondsPerClock;

	bool m_bIsStopped;
};


void StartTimer(FrameTimer& timer);
void StopTimer(FrameTimer& timer);
void ResetTimer(FrameTimer& timer);
void TickTimer(FrameTimer& timer);

f32 GetDeltaTimeS(FrameTimer& timer);
f32 GetTotalTimeS(FrameTimer& timer);