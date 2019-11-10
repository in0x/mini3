#pragma once

#include "InputMessageQueue.h"

class BaseApp;
struct WindowConfig;

struct Win32Window
{
public:
	bool Init(WindowConfig const& config);
	bool Run();
	void Exit();

	ThreadSafeInputMessageQueue m_msgQueue;
	void* m_mainWindowHandle;
};