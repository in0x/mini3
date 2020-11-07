#pragma once
#include <stdint.h>
#include "Core.h"
#include "FrameTimer.h"

class IMessageQueueConsumer;
struct WindowConfig;

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

	void SetWindowCfg(WindowConfig const* window_cfg);

	// Returns whether the stats where refreshed since the last read.
	bool GetStats(f32& out_avg_fps, f32& out_avg_ms_per_frame);

protected:
	FrameTimer m_timer;
	WindowConfig const* m_window_cfg;
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