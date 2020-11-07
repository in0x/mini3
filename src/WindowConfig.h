#pragma once
#include "Core.h"

struct WindowConfig
{
	u32 width;
	u32 height;
	u32 left;
	u32 top;
	const char* title;
	bool b_fullscreen;
	bool b_auto_show;
};
