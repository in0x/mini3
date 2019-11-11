#include "Win32Window.h"
#include "Core.h"
#include "BaseApp.h"
#include "Win32.h"

static bool IsWindowClassValid(ATOM classHandle)
{
	return classHandle != 0;
}

static ATOM RegisterWindowClass(char const* className, WNDPROC eventCb)
{
	WNDCLASSEX windowClass = {};
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	windowClass.lpfnWndProc = eventCb;
	windowClass.hInstance = GetModuleHandle(nullptr);
	windowClass.lpszClassName = className;

	ATOM classHandle = RegisterClassEx(&windowClass);

	if (!IsWindowClassValid(classHandle))
	{
		LOG("Failed to register window class type %s!\n", className);
		LogLastWindowsError();
	}

	return classHandle;
}

static bool UnregisterWindowClass(char const* className)
{
	UnregisterClass(className, GetModuleHandle(nullptr));
}

static LRESULT CALLBACK OnMainWindowEvent(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);

static char const * const s_mainWindowClassName = "mini3::Win32Window";

bool Win32Window::Init(WindowConfig const& config)
{
	RegisterWindowClass(s_mainWindowClassName, &OnMainWindowEvent);

	RECT windowRect = {};
	windowRect.left = config.left;
	windowRect.top = config.top;
	windowRect.bottom = config.top + config.height;
	windowRect.right = config.left + config.height;

	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	DWORD exWindStyle = WS_EX_APPWINDOW;
	DWORD windStyle = 0;

	if (config.bFullscreen)
	{
		DEVMODE displayConfig = {};
		EnumDisplaySettings(nullptr, ENUM_REGISTRY_SETTINGS, &displayConfig);

		if (ChangeDisplaySettings(&displayConfig, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
		{
			LOG("Failed to fullscreen the window.\n");
			return false;
		}

		windStyle = WS_POPUP;
	}
	else
	{
		exWindStyle |= WS_EX_WINDOWEDGE;
		windStyle = WS_OVERLAPPEDWINDOW;

		// Reconfigures window rect to factor in size of border.
		AdjustWindowRectEx(&windowRect, windStyle, FALSE, exWindStyle);
	}

	m_mainWindowHandle = CreateWindowEx(
		exWindStyle,
		s_mainWindowClassName,
		config.title,
		WS_CLIPSIBLINGS | WS_CLIPCHILDREN | windStyle,
		config.left, config.top,
		config.width, config.height,
		nullptr, // Parent window
		nullptr,
		GetModuleHandle(nullptr),
		this);

	if (m_mainWindowHandle == nullptr)
	{
		LogLastWindowsError();
		return false;
	}

	if (config.bAutoShow)
	{
		ShowWindow((HWND)m_mainWindowHandle, SW_SHOW);
		SetForegroundWindow((HWND)m_mainWindowHandle);
	}

	UpdateWindow((HWND)m_mainWindowHandle);
	
	LOG("Created window TITLE: %s WIDTH: %u HEIGHT: %u FULLSCREEN: %d", config.title, config.width, config.height, static_cast<s32>(config.bFullscreen));

	return m_mainWindowHandle != nullptr;
}

bool Win32Window::Run()
{
	MSG msg = {};
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		if (WM_QUIT == msg.message)
		{
			m_msgQueue.AddQuitMessage();
			return false;
		}

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return true;
}

void Win32Window::Exit()
{
	UnregisterClass(s_mainWindowClassName, GetModuleHandle(nullptr));
}

static LRESULT CALLBACK OnMainWindowEvent(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
{
	// TODO(pw): We should buffer all of this and run the main app on another thread.
	// Then at fixed points pump this to the window.

	// WPARAM -> Word parameter, carries "words" i.e. handle, integers
	// LAPARM -> Long paramter -> carries pointers

	Win32Window* window = reinterpret_cast<Win32Window*>(GetWindowLongPtr(handle, GWLP_USERDATA));

	switch (message)
	{
	case WM_CREATE:
	{
		LPCREATESTRUCT createStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
		SetWindowLongPtr(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
		break;
	}

	case WM_CLOSE:
	{
		PostQuitMessage(0);
		break;
	}
	
	case WM_SIZE:
	{
		break;
	}
	
	case WM_KEYDOWN:
	{
		window->m_msgQueue.AddKeyMessage(static_cast<s8>(wParam), true);
		break;
	}
	
	case WM_KEYUP:
	{
		window->m_msgQueue.AddKeyMessage(static_cast<s8>(wParam), false);
		break;
	}
	}
	return DefWindowProc(handle, message, wParam, lParam);
}