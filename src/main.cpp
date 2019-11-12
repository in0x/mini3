#include "Core.h"

#include "Win32.h"
#include "Win32Window.h"
#include "MiniApp.h"
#include <thread>

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

	LOG(Log::Default, "Initializing mini3");

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

	size_t const windowTitleLength = 128;
	char windowTitle[128] = {0};

	// We run the Win32Window on our "main/UI" thread.
	while (window.Run())
	{
		f32 avgFps, avgMs;
		if (app.GetStats(avgFps, avgMs))
		{
			MiniPrintf(windowTitle, windowTitleLength, "%s - %f fps | %f.2 ms/frame", false, config.title, avgFps, avgMs);

			SetWindowText((HWND)window.m_mainWindowHandle, windowTitle);
		}
	}

	appthread.join();
	window.Exit();

	return 0;
}