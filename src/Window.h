#pragma once
#include "Core.h"
#include "Windows.h"

//TODO(pw) Should probably encapsulate this so we can get rid of the exposed windows header.

class WindowClass
{
	enum { MAX_NAME_LENGTH = 64 };

	char m_className[MAX_NAME_LENGTH];
	WNDPROC m_eventCb;

public:
	WindowClass(const char* className, WNDPROC eventCb);
	~WindowClass();

	bool RegisterWindowClass();
	void unregisterClass();
	const char* getClassName() const;
};

LRESULT CALLBACK OnMainWindowEvent(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);

struct WindowConfig
{
	uint32_t width;
	uint32_t height;
	uint32_t left;
	uint32_t top;
	const char* title;
	bool bFullscreen;
	bool bAutoShow;
};

HWND CreateMiniWindow(WindowConfig const& config, WindowClass const& windowClass);