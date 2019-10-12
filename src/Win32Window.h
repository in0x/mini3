#pragma once

#include "Win32.h"

class BaseApp;
struct WindowConfig;

struct Win32Window
{
public:
	bool Init(WindowConfig const& config, BaseApp* window);
	void Run();
	void Exit();

private:
	HWND m_mainWindowHandle;
	BaseApp* m_app;
};