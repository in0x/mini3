#include <stdio.h>
#include <stdint.h>

#include <string>
#include <atomic>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
	#define PLATFORM_WINDOWS
#endif

#ifndef PLATFORM_WINDOWS
	static_assert(false, __FILE__ " may only be included on a Windows Platform");
#else
	#ifndef NOMINMAX 
	#define NOMINMAX 
	#endif  

	#define WIN32_LEAN_AND_MEAN

	#include <Windows.h>
#endif

namespace mini
{
	void LogLastWindowsError()
	{
		LPTSTR errorText = NULL;
		DWORD lastError = GetLastError();

		FormatMessage(
			// use system message tables to retrieve error text
			FORMAT_MESSAGE_FROM_SYSTEM
			// allocate buffer on local heap for error text
			| FORMAT_MESSAGE_ALLOCATE_BUFFER
			// Important! will fail otherwise, since we're not 
			// (and CANNOT) pass insertion parameters
			| FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,    // unused with FORMAT_MESSAGE_FROM_SYSTEM
			lastError,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR)&errorText,  // output 
			0, // minimum size for output buffer
			NULL);   // arguments - see note 

		if (errorText != nullptr)
		{
			printf("%s", errorText);

			LocalFree(errorText);
		}
		else
		{
			printf("Failed to get message for last windows error %u", static_cast<uint32_t>(lastError));
		}
	}

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

	class WindowClass
	{
		enum { MAX_TITLE_LENGTH = 64 };

		char m_className[MAX_TITLE_LENGTH];
		WNDPROC m_eventCb;
		
	public:
		WindowClass(const char* className, WNDPROC eventCb)
			: m_eventCb(eventCb)
		{
			strncpy_s(m_className, className, MAX_TITLE_LENGTH);

			if (strlen(m_className) >= MAX_TITLE_LENGTH)
			{
				m_className[MAX_TITLE_LENGTH - 1] = '\n';
			}
		}

		~WindowClass()
		{
			unregisterClass();
		}

		bool registerClass()
		{
			WNDCLASSEX windowClass = {};

			windowClass.cbSize = sizeof(WNDCLASSEX);
			windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
			windowClass.lpfnWndProc = m_eventCb;
			windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
			windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
			windowClass.hInstance = GetModuleHandle(nullptr);
			windowClass.lpszClassName = m_className;
			
			ATOM classHandle = RegisterClassEx(&windowClass);

			if (classHandle == 0)
			{
				printf("Failed to register window class type %s!\n", m_className);
				LogLastWindowsError();
			}

			return classHandle != 0;
		}

		void unregisterClass()
		{
			UnregisterClass(m_className, GetModuleHandle(nullptr));
		}

		const char* getClassName() const
		{
			return m_className;
		}
	};

	HWND createWindow(const WindowConfig& config, const WindowClass& windowClass)
	{
		RECT windowRect = {};
		windowRect.left = config.left;
		windowRect.top = config.top;
		windowRect.bottom = config.top + config.height;
		windowRect.right = config.left + config.height;
	
		AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

		DWORD exWindStyle = 0;
		DWORD windStyle = 0;

		if (config.bFullscreen)
		{
			DEVMODE displayConfig = {};
			
			EnumDisplaySettings(nullptr, ENUM_REGISTRY_SETTINGS, &displayConfig);

			if (ChangeDisplaySettings(&displayConfig, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
			{
				printf("Failed to fullscreen the window.\n");
				return nullptr;
			}

			exWindStyle = WS_EX_APPWINDOW;
			windStyle = WS_POPUP;
		}
		else
		{
			exWindStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
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
			printf("Failed to create window!\n");
			LogLastWindowsError();
			return nullptr;
		}

		if (config.bAutoShow)
		{
			ShowWindow(window, SW_SHOW);
		}

		return window;
	}

	struct WindowState // TODO Produce a window state from windows message pump and feed to application
	{
		enum
		{
			InResize = 1 << 0,
			ResizeFinished = 1 << 1,
			Suspended = 1 << 2,
			Activated = 1 << 3,
			Deactivated = 1 << 4
		};

		uint32_t m_flags;
	};

	class Application
	{
		void OnMinimize();
		void OnSuspend();
		void OnResume();
		void OnSizeChanged(uint32_t width, uint32_t height);
	};
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
		//OnResize(LOWORD(lParam), HIWORD(lParam));
		break;

	case WM_KEYDOWN:
		break;

	case WM_KEYUP:
		break;
	}
	return DefWindowProc(handle, message, wParam, lParam);
}


int main(int argc, char** argv)
{
	printf("Hello, mini3\n");

	mini::WindowClass mainWindowClass("mini3::Window", &OnMainWindowEvent);

	if (!mainWindowClass.registerClass())
	{
		printf("Failed to register main window class!");
		return -1;
	}

	mini::WindowConfig config;
	config.width = 1200;
	config.height = 700;
	config.left = 0;
	config.top = 0;
	config.title = "mini3";
	config.bFullscreen = false;
	config.bAutoShow = true;

	HWND window = mini::createWindow(config, mainWindowClass);

	if (window == nullptr)
	{
		printf("Failed to create main window!");
		return -1;
	}

	//SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(g_game.get()));
	//GetClientRect(hwnd, &rc);

	MSG msg = {};
	while (WM_QUIT != msg.message)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			
		}
	}


	return 0;
}