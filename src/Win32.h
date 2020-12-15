#pragma once

#include "Core.h"

#ifndef NOMINMAX 
#define NOMINMAX 
#endif  

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <wrl/client.h>
#include <wrl/event.h>
#include <comdef.h>

void LogLastWindowsError();

#ifdef _DEBUG
#define ASSERT_HR(hr) if(SUCCEEDED(hr) == false) { _com_error err(hr); ASSERT_FAIL_F("%s", err.ErrorMessage()); }
#define ASSERT_HR_F(hr, format, ...) ASSERT_F(SUCCEEDED(hr), format, __VA_ARGS__)
#else
#define ASSERT_HR(hr) UNUSED(hr)
#define ASSERT_HR_F(hr, format, ...) UNUSED(hr)
#endif

#ifdef _DEBUG
#define VERIFY_HR(x)			\
	{								\
		HRESULT verify_hr_hr = (x);	\
		ASSERT_HR(verify_hr_hr);	\
	}
#else
#define VERIFY_HR(x) x
#endif