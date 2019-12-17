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
	bool b_fullscreen;
	bool b_auto_show;
};

class IMessageQueueConsumer;

class BaseApp
{
public:
	BaseApp();

	virtual void Init();
	virtual bool Update();
	virtual void Exit();

	void SetNativeHandle(void* native_handle);
	void* GetNativeHandle();

	void SetMessageQueue(IMessageQueueConsumer* queue);

	// Returns whether the stats where refreshed since the last read.
	bool GetStats(f32& out_avg_fps, f32& out_avg_ms_per_frame);

protected:
	FrameTimer m_timer;
	IMessageQueueConsumer* m_msg_queue;

private:
	struct
	{
		f32 counter;
		f32 elapsed_time;
		f32 avg_fps;
		f32 avg_ms;
		bool m_b_stats_refreshed;
	} m_frameStats = { 0 };

	void UpdateFrameStats();
	
	void* m_native_handle;
	bool m_b_is_paused;
};