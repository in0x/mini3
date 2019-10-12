#pragma once
#include <stdint.h>

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

class BaseApp
{
public:
	virtual void Init() = 0;
	virtual void Update() = 0;
	virtual void Render() = 0;
	virtual void Exit() = 0;

	virtual void OnKeyDown(uint8_t key) {}
	virtual void OnKeyUp(uint8_t key) {}

	//virtual void OnMouseDown(WPARAM btnState, int32_t x, int32_t y) {}
	//virtual void OnMouseUp(WPARAM btnState, int32_t x, int32_t y) {}
	//virtual void OnMouseMove(WPARAM btnState, int32_t x, int32_t y) {}

	void SetNativeHandle(void* nativeHandle);
	void* GetNativeHandle();

private:
	void* m_NativeHandle;
};