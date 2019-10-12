#include "Core.h"

#include "Win32Window.h"
#include "MiniApp.h"

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
	
	window.Init(config, &app);
	window.Run();
	window.Exit();

	return 0;
}