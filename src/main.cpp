#include "Core.h"

#include "Win32.h"
#include "Win32Window.h"
#include "MiniApp.h"
#include <thread>
#include <string>

void AppthreadMain(BaseApp* app)
{
	app->Init();

	while (app->Update()) ;
	
	app->Exit();
}

INT WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow)
{
	UNUSED(hInstance);
	UNUSED(hPrevInstance);
	UNUSED(lpCmdLine);
	UNUSED(nCmdShow);

	LOG("Initializing mini3");

	WindowConfig config;
	config.width = 800;
	config.height = 600;
	config.left = 0;
	config.top = 0;
	config.title = "mini3";
	config.bFullscreen = false;
	config.bAutoShow = true;

	MiniApp app;
	Win32Window window;
	
	window.Init(config);

	app.SetNativeHandle(window.m_mainWindowHandle);
	app.SetMessageQueue(&window.m_msgQueue);

	// We run the app on the "App" thread to avoid blocking during message pump.
	std::thread appthread(AppthreadMain, &app);

	// We run the Win32Window on our "main/UI" thread.
	while (window.Run())
	{
		f32 avgFps, avgMs;
		if (app.GetStats(avgFps, avgMs))
		{
			std::string fpsStr = std::to_string(avgFps);
			std::string mspfStr = std::to_string(avgMs);

			std::string windowText = config.title;
			windowText += "    fps: " + fpsStr;
			windowText += "   ms/frame: " + mspfStr;

			SetWindowText((HWND)window.m_mainWindowHandle, windowText.c_str());
		}
	}

	appthread.join();
	window.Exit();

	return 0;
}