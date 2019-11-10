#pragma once
#include <stdint.h>
#include "Core.h"
#include "FrameTimer.h"

struct WindowConfig
{
	u32 width;
	u32 height;
	u32 left;
	u32 top;
	const char* title;
	bool bFullscreen;
	bool bAutoShow;
};

class IMessageQueueConsumer;

class BaseApp
{
public:
	BaseApp();

	virtual void Init();
	virtual bool Update();
	virtual void Exit();

	void SetNativeHandle(void* nativeHandle);
	void* GetNativeHandle();

	void SetMessageQueue(IMessageQueueConsumer* queue);

	// Returns whether the stats where refreshed since the last read.
	bool GetStats(f32& outAvgFps, f32& outAvgMsPerFrame);

protected:
	FrameTimer m_timer;
	IMessageQueueConsumer* m_msgQueue;

private:
	struct
	{
		f32 counter;
		f32 elapsedTime;
		f32 avgFps;
		f32 avgMs;
		bool m_bStatsRefreshed;
	} m_frameStats = { 0 };

	void UpdateFrameStats();
	
	void* m_nativeHandle;
	bool m_bIsPaused;
};