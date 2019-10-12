#include "Core.h"
#include "Windows.h"

#include "DeviceResources.h"

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
	enum { MAX_NAME_LENGTH = 64 };

	char m_className[MAX_NAME_LENGTH];
	WNDPROC m_eventCb;
		
public:
	WindowClass(const char* className, WNDPROC eventCb)
		: m_eventCb(eventCb)
	{
		strncpy_s(m_className, className, MAX_NAME_LENGTH);

		if (strlen(m_className) >= MAX_NAME_LENGTH)
		{
			m_className[MAX_NAME_LENGTH - 1] = '\n';
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
		windowClass.hInstance = GetModuleHandle(nullptr);
		windowClass.lpszClassName = m_className;
			
		ATOM classHandle = RegisterClassEx(&windowClass);

		if (classHandle == 0)
		{
			LOG("Failed to register window class type %s!\n", m_className);
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

	DWORD exWindStyle = WS_EX_APPWINDOW;
	DWORD windStyle = 0;

	if (config.bFullscreen)
	{
		DEVMODE displayConfig = {};		
		EnumDisplaySettings(nullptr, ENUM_REGISTRY_SETTINGS, &displayConfig);

		if (ChangeDisplaySettings(&displayConfig, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
		{
			LOG("Failed to fullscreen the window.\n");
			return nullptr;
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
		LogLastWindowsError();
		return nullptr;
	}

	if (config.bAutoShow)
	{
		ShowWindow(window, SW_SHOW);
	}

	LOG("Created window TITLE: %s WIDTH: %u HEIGHT: %u FULLSCREEN: %d", config.title, config.width, config.height, static_cast<int32_t>(config.bFullscreen));

	return window;
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
		break;

	case WM_KEYDOWN:
		break;

	case WM_KEYUP:
		break;
	}
	return DefWindowProc(handle, message, wParam, lParam);
}

void present(DeviceResources* adapter)
{
	ComPtr<ID3D12CommandAllocator> allocator = adapter->GetCommandAllocator();
	ComPtr<ID3D12GraphicsCommandList> cmdList = adapter->GetCommandList();
	ComPtr<ID3D12CommandQueue> cmdQueue = adapter->GetCommandQueue();
	ComPtr<IDXGISwapChain3> swapChain = adapter->GetSwapChain();

	allocator->Reset();
	cmdList->Reset(allocator.Get(), nullptr);

	D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_PRESENT;
	if (beforeState != D3D12_RESOURCE_STATE_RENDER_TARGET)
	{
		// Transition the render target into the correct state to allow for drawing into it.
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(adapter->GetRenderTarget(), beforeState, D3D12_RESOURCE_STATE_RENDER_TARGET);
		cmdList->ResourceBarrier(1, &barrier);
	}

	// Clear the backbuffer
	// Clear the views. 
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptor = adapter->GetRenderTargetView();
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvDescriptor = adapter->GetDepthStencilView();

	float blueViolet[4] = { 0.541176498f, 0.168627456f, 0.886274576f, 1.000000000f };
	cmdList->OMSetRenderTargets(1, &rtvDescriptor, FALSE, &dsvDescriptor);
	cmdList->ClearRenderTargetView(rtvDescriptor, blueViolet, 0, nullptr);
	cmdList->ClearDepthStencilView(dsvDescriptor, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// Set the viewport and scissor rect.
	D3D12_VIEWPORT viewport = adapter->GetScreenViewport();
	D3D12_RECT scissorRect = adapter->GetScissorRect();
	cmdList->RSSetViewports(1, &viewport);
	cmdList->RSSetScissorRects(1, &scissorRect);

	beforeState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	if (beforeState != D3D12_RESOURCE_STATE_PRESENT)
	{
		// Transition the render target to the state that allows it to be presented to the display.
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(adapter->GetRenderTarget(), beforeState, D3D12_RESOURCE_STATE_PRESENT);
		cmdList->ResourceBarrier(1, &barrier);
	}

	// Send the command list off to the GPU for processing.
	HRESULT hr = cmdList->Close();
	ASSERT_RESULT(hr);
	cmdQueue->ExecuteCommandLists(1, CommandListCast(cmdList.GetAddressOf()));

	if (adapter->IsTearingAllowed())
	{
		// Recommended to always use tearing if supported when using a sync interval of 0.
		// Note this will fail if in true 'fullscreen' mode.
		hr = swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
	}
	else
	{
		// The first argument instructs DXGI to block until VSync, putting the application
		// to sleep until the next VSync. This ensures we don't waste any cycles rendering
		// frames that will never be displayed to the screen.
		hr = swapChain->Present(1, 0);
	}

	// If the device was reset we must completely reinitialize the renderer.
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
		char err[64] = {};
		sprintf_s(err, "Device Lost on ResizeBuffers: Reason code 0x%08X\n", (hr == DXGI_ERROR_DEVICE_REMOVED) ? adapter->GetD3DDevice()->GetDeviceRemovedReason() : hr);
		ASSERT_F(false, err);
	}
	else
	{
		ASSERT_RESULT(hr);
	}

	// Prepare to render the next frame.
	uint64_t currentFenceValue = adapter->GetCurrentFenceValue();
	ID3D12Fence* fence = adapter->GetFence();
	HANDLE fenceEvent = adapter->GetFenceEvent();
	
	hr = cmdQueue->Signal(fence, currentFenceValue);
	ASSERT_RESULT(hr);

	adapter->m_backBufferIndex = adapter->m_swapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (adapter->m_fence->GetCompletedValue() < currentFenceValue)
	{
		hr = fence->SetEventOnCompletion(currentFenceValue, fenceEvent);
		ASSERT_RESULT(hr);
		WaitForSingleObjectEx(adapter->GetFenceEvent(), INFINITE, FALSE);
	}

	adapter->SetNextFenceValue();

	if (!adapter->m_dxgiFactory->IsCurrent())
	{
		// Output information is cached on the DXGI Factory. If it is stale we need to create a new factory.
		hr = CreateDXGIFactory2(adapter->m_dxgiFactoryFlags, IID_PPV_ARGS(adapter->m_dxgiFactory.ReleaseAndGetAddressOf()));
		ASSERT_RESULT(hr);
	}
}

INT WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow)
{
	UNUSED(hInstance);
	UNUSED(hPrevInstance);
	UNUSED(lpCmdLine);
	UNUSED(nCmdShow);

	LOG("Initializing mini3");

	WindowClass mainWindowClass("mini3::Window", &OnMainWindowEvent);

	if (!mainWindowClass.registerClass())
	{
		LOG("Failed to register main window class!");
		return -1;
	}

	WindowConfig config;
	config.width = 1200;
	config.height = 700;
	config.left = 0;
	config.top = 0;
	config.title = "mini3";
	config.bFullscreen = false;
	config.bAutoShow = true;

	HWND window = createWindow(config, mainWindowClass);

	if (window == nullptr)
	{
		LOG("Failed to create main window!");
		return -1;
	}

	//SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(g_game.get()));
	//GetClientRect(hwnd, &rc);

	DeviceResources resources;
	resources.init(window, DeviceResources::IF_EnableDebugLayer | DeviceResources::IF_AllowTearing);
	
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
			present(&resources);
		}
	}

	return 0;
}