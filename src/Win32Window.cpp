#include "Win32Window.h"
#include "Core.h"
#include "BaseApp.h"
#include "Win32.h"
#include "WindowConfig.h"

static bool IsWindowClassValid(ATOM classHandle)
{
	return classHandle != 0;
}

static ATOM RegisterWindowClass(char const* class_name, WNDPROC event_cb)
{
	WNDCLASSEX windowClass = {};
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	windowClass.lpfnWndProc = event_cb;
	windowClass.hInstance = GetModuleHandle(nullptr);
	windowClass.lpszClassName = class_name;

	ATOM classHandle = RegisterClassEx(&windowClass);

	if (!IsWindowClassValid(classHandle))
	{
		LOG(Log::Win32, "Failed to register window class type %s!\n", class_name);
		LogLastWindowsError();
	}

	return classHandle;
}

static bool UnregisterWindowClass(char const* className)
{
	UnregisterClass(className, GetModuleHandle(nullptr));
}

static LRESULT CALLBACK OnMainWindowEvent(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);

static char const * const s_main_window_class_name = "mini3::Win32Window";

bool Win32Window::Init(WindowConfig const& config)
{
	RegisterWindowClass(s_main_window_class_name, &OnMainWindowEvent);

	RECT windowRect = {};
	windowRect.left = config.left;
	windowRect.top = config.top;
	windowRect.bottom = config.top + config.height;
	windowRect.right = config.left + config.height;

	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	DWORD exWindStyle = WS_EX_APPWINDOW;
	DWORD windStyle = 0;

	if (config.b_fullscreen)
	{
		DEVMODE displayConfig = {};
		EnumDisplaySettings(nullptr, ENUM_REGISTRY_SETTINGS, &displayConfig);

		if (ChangeDisplaySettings(&displayConfig, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
		{
			LOG(Log::Win32, "Failed to fullscreen the window.\n");
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

	m_main_window_handle = CreateWindowEx(
		exWindStyle,
		s_main_window_class_name,
		config.title,
		WS_CLIPSIBLINGS | WS_CLIPCHILDREN | windStyle,
		config.left, config.top,
		config.width, config.height,
		nullptr, // Parent window
		nullptr,
		GetModuleHandle(nullptr),
		this);

	if (m_main_window_handle == nullptr)
	{
		LogLastWindowsError();
		return false;
	}

	if (config.b_auto_show)
	{
		ShowWindow((HWND)m_main_window_handle, SW_SHOW);
		SetForegroundWindow((HWND)m_main_window_handle);
	}

	UpdateWindow((HWND)m_main_window_handle);
	
	LOG(Log::Win32, "Created window TITLE: %s WIDTH: %u HEIGHT: %u FULLSCREEN: %d", config.title, config.width, config.height, static_cast<s32>(config.b_fullscreen));

	return m_main_window_handle != nullptr;
}

bool Win32Window::Run()
{
	MSG msg = {};
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		if (WM_QUIT == msg.message)
		{
			m_msg_queue.AddQuitMessage();
			return false;
		}

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return true;
}

void Win32Window::Exit()
{
	UnregisterClass(s_main_window_class_name, GetModuleHandle(nullptr));
}

static KeyCode::Enum MapVKToKeyCode(WPARAM key_code)
{
	switch (key_code)
	{
	case 0x25:
		return KeyCode::ARROW_LEFT;
	case 0x26:
		return KeyCode::ARROW_UP;
	case 0x27:
		return KeyCode::ARROW_RIGHT;
	case 0x28:
		return KeyCode::ARROW_DOWN;
	case 0x41:
		return KeyCode::A;
	case 0x44:
		return KeyCode::D;
	case 0x53:
		return KeyCode::S;
	case 0x57:
		return KeyCode::W;
	default:
		return KeyCode::UNKNOWN;
	}
}

static KeyCode::Enum MapEventToMouse(UINT ev)
{
	switch (ev)
	{
	case WM_LBUTTONDOWN: return KeyCode::MSB_LEFT;
	case WM_RBUTTONDOWN: return KeyCode::MSB_RIGHT;
	case WM_MBUTTONDOWN: return KeyCode::MSB_MIDDLE;
	default: return KeyCode::UNKNOWN;
	}
}

static LRESULT CALLBACK OnMainWindowEvent(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
{
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
		window->m_msg_queue.AddKeyChange(MapVKToKeyCode(wParam), true);
		break;
	}
	
	case WM_KEYUP:
	{
		window->m_msg_queue.AddKeyChange(MapVKToKeyCode(wParam), false);
		break;
	}
	
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	{
		window->m_msg_queue.AddMouseChange(
			KeyMsg::KeyDown, 
			MapEventToMouse(message),
			GET_X_LPARAM(lParam), 
			GET_Y_LPARAM(lParam));
		
		SetCapture((HWND)window->m_main_window_handle);
		break;
	}

	case WM_MOUSEMOVE:
	{
		if (wParam == 0) break;

		if (wParam & MK_LBUTTON)
		{
			window->m_msg_queue.AddMouseChange(
				KeyMsg::MouseMove,
				KeyCode::MSB_LEFT,
				GET_X_LPARAM(lParam),
				GET_Y_LPARAM(lParam));
		}

		if (wParam & MK_RBUTTON)
		{
			window->m_msg_queue.AddMouseChange(
				KeyMsg::MouseMove,
				KeyCode::MSB_RIGHT,
				GET_X_LPARAM(lParam),
				GET_Y_LPARAM(lParam));
		}
		
		break;
	}

	case WM_MOUSEWHEEL:
	{
		window->m_msg_queue.AddMouseWheelChange(GET_WHEEL_DELTA_WPARAM(wParam));
		break;
	}

	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
	{
		window->m_msg_queue.AddMouseChange(
			KeyMsg::KeyUp, 
			MapEventToMouse(message),
			GET_X_LPARAM(lParam),
			GET_Y_LPARAM(lParam));
		
		ReleaseCapture();
		break;
	}

	}
	return DefWindowProc(handle, message, wParam, lParam);
}
