#include "BaseApp.h"

void BaseApp::SetNativeHandle(void* nativeHandle)
{
	m_NativeHandle = nativeHandle;
}

void* BaseApp::GetNativeHandle()
{
	return m_NativeHandle;
}