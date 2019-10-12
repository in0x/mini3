#include "Window.h"

WindowClass::WindowClass(const char* className, WNDPROC eventCb)
	: m_eventCb(eventCb)
{
	strncpy_s(m_className, className, MAX_NAME_LENGTH);

	if (strlen(m_className) >= MAX_NAME_LENGTH)
	{
		m_className[MAX_NAME_LENGTH - 1] = '\n';
	}
}

WindowClass::~WindowClass()
{
	unregisterClass();
}

bool WindowClass::RegisterWindowClass()
{
	WNDCLASSEX windowClass = {};
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	windowClass.lpfnWndProc = m_eventCb;
	windowClass.hInstance = GetModuleHandle(nullptr);
	windowClass.lpszClassName = m_className;

	ATOM classHandle = RegisterClassEx(&windowClass);

	if (classHandle == 0)
	{
		LOG("Failed to register window class type %s!\n", m_className);
		LogLastWindowsError();
	}

	return classHandle != 0;
}

void WindowClass::unregisterClass()
{
	UnregisterClass(m_className, GetModuleHandle(nullptr));
}

const char* WindowClass::getClassName() const
{
	return m_className;
}

LRESULT CALLBACK OnMainWindowEvent(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
{
	// WPARAM -> Word parameter, carries "words" i.e. handle, integers
	// LAPARM -> Long paramter -> carries pointers

	if (message == WM_NCCREATE)
	{
		auto pCreateParams = reinterpret_cast<CREATESTRUCT*>(lParam);
		SetWindowLongPtr(handle, GWLP_USERDATA, reinterpret_cast<uintptr_t>(pCreateParams->lpCreateParams));
	}

	//auto pWindow = reinterpret_cast<Win32Window*>(GetWindowLongPtr(handle, GWLP_USERDATA));

	switch (message)
	{
	case WM_CLOSE:
		PostQuitMessage(0);
		break;

	case WM_SIZE:
		break;

	case WM_KEYDOWN:
		break;

	case WM_KEYUP:
		break;
	}
	return DefWindowProc(handle, message, wParam, lParam);
}

HWND CreateMiniWindow(WindowConfig const& config, WindowClass const& windowClass)
{
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
			return nullptr;
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

	HWND window = CreateWindowEx(
		exWindStyle,
		windowClass.getClassName(),
		config.title,
		WS_CLIPSIBLINGS | WS_CLIPCHILDREN | windStyle,
		config.left, config.top,
		config.width, config.height,
		nullptr, // Parent window
		nullptr,
		GetModuleHandle(nullptr),
		nullptr);

	if (window == nullptr)
	{
		LogLastWindowsError();
		return nullptr;
	}

	if (config.bAutoShow)
	{
		ShowWindow(window, SW_SHOW);
	}

	LOG("Created window TITLE: %s WIDTH: %u HEIGHT: %u FULLSCREEN: %d", config.title, config.width, config.height, static_cast<int32_t>(config.bFullscreen));

	return window;
}