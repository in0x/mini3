#include "Core.h"

#include "Window.h"
#include "Windows.h"
#include "DeviceResources.h"

void Present(DeviceResources* resources)
{
	ComPtr<ID3D12CommandAllocator> allocator = resources->GetCommandAllocator();
	ComPtr<ID3D12GraphicsCommandList> cmdlist = resources->GetCommandList();
	ComPtr<ID3D12CommandQueue> cmdqueue = resources->GetCommandQueue();
	ComPtr<IDXGISwapChain3> swapchain = resources->GetSwapChain();

	allocator->Reset();
	cmdlist->Reset(allocator.Get(), nullptr);
	
	// Transition the render target into the correct state to allow for drawing into it.
	{
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			resources->GetCurrentRenderTarget(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		);

		cmdlist->ResourceBarrier(1, &barrier);
	}

	// Clear the backbuffer and views. 
	{
		float const blueViolet[4] = { 0.541176498f, 0.168627456f, 0.886274576f, 1.000000000f };

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptor = resources->GetRenderTargetView();
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvDescriptor = resources->GetDepthStencilView();

		cmdlist->OMSetRenderTargets(1, &rtvDescriptor, FALSE, &dsvDescriptor); // todo: do we need this?
		cmdlist->ClearRenderTargetView(rtvDescriptor, blueViolet, 0, nullptr);
		cmdlist->ClearDepthStencilView(dsvDescriptor, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	}
	
	// Set the viewport and scissor rect.
	{
		D3D12_VIEWPORT viewport = resources->GetScreenViewport();
		D3D12_RECT scissorRect = resources->GetScissorRect();
		cmdlist->RSSetViewports(1, &viewport);
		cmdlist->RSSetScissorRects(1, &scissorRect);
	}
	
	// Transition the render target to the state that allows it to be presented to the display.
	{
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			resources->GetCurrentRenderTarget(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT
		);

		cmdlist->ResourceBarrier(1, &barrier);
	}
	
	// Send the command list off to the GPU for processing.
	HRESULT hr = cmdlist->Close();
	ASSERT_RESULT(hr);

	ID3D12CommandList* ppCommandLists[] = { cmdlist.Get() };
	cmdqueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	if (resources->IsTearingAllowed())
	{
		// Recommended to always use tearing if supported when using a sync interval of 0.
		// Note this will fail if in true 'fullscreen' mode.
		hr = swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
	}
	else
	{
		// The first argument instructs DXGI to block until VSync, putting the application
		// to sleep until the next VSync. This ensures we don't waste any cycles rendering
		// frames that will never be displayed to the screen.
		hr = swapchain->Present(1, 0);
	}

	// If the device was reset we must completely reinitialize the renderer.
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
		char err[64] = {};
		sprintf_s(err, "Device Lost on ResizeBuffers: Reason code 0x%08X\n", (hr == DXGI_ERROR_DEVICE_REMOVED) ? resources->GetD3DDevice()->GetDeviceRemovedReason() : hr);
		ASSERT_F(false, err);
	}
	else
	{
		ASSERT_RESULT(hr);
	}

	resources->Flush();
}

INT WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow)
{
	UNUSED(hInstance);
	UNUSED(hPrevInstance);
	UNUSED(lpCmdLine);
	UNUSED(nCmdShow);

	LOG("Initializing mini3");

	WindowClass windowClass("mini3::Window", &OnMainWindowEvent);

	if (!windowClass.RegisterWindowClass())
	{
		LOG("Failed to register main window class!");
		return -1;
	}

	WindowConfig config;
	config.width = 800;
	config.height = 600;
	config.left = 0;
	config.top = 0;
	config.title = "mini3";
	config.bFullscreen = false;
	config.bAutoShow = true;

	HWND window = CreateMiniWindow(config, windowClass);

	if (window == nullptr)
	{
		LOG("Failed to create main window!");
		return -1;
	}

	//SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(g_game.get()));
	//GetClientRect(hwnd, &rc);

	DeviceResources resources;
	resources.Init(window, DeviceResources::IF_EnableDebugLayer | DeviceResources::IF_AllowTearing);
	
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
			Present(&resources);
		}
	}

	return 0;
}