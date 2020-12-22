#include "Core.h"

#include "Win32.h"
#include "Win32Window.h"
#include "WindowConfig.h"
#include "MiniApp.h"
#include "Math.h"
#include "Memory.h"
#include "IO.h"

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

	LOG(Log::Default, "Initing Memory System");
	Memory::Init();

	LOG(Log::Default, "Initing File System");
	IO::FileSysInit("C:\\Users\\Philipp\\Documents\\work\\mini3"); // TODO(): Get this from a cvar

	LOG(Log::Default, "Running Unit Tests");
	Math::Test::Run();

	LOG(Log::Default, "Initializing mini3");

	WindowConfig config;
	config.width = 800;
	config.height = 600;
	config.left = 0;
	config.top = 0;
	config.title = "mini3";
	config.b_fullscreen = false;
	config.b_auto_show = true;

	MiniApp app;
	Win32Window window;
	
	window.Init(config);

	app.SetNativeHandle(window.m_main_window_handle);
	app.SetMessageQueue(&window.m_msg_queue);
	app.SetWindowCfg(&config);

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
			SetWindowText((HWND)window.m_main_window_handle, windowTitle);
		}
	}
	
	appthread.join();
	window.Exit();

	IO::FileSysExit();
	Memory::Exit();

	return 0;
}