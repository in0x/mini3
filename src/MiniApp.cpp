#include "MiniApp.h"

#include "Win32.h"
#include "DeviceResources.h"

void MiniApp::Init()
{
	m_resources = new DeviceResources();

	HWND windowHandle = static_cast<HWND>(GetNativeHandle());
	m_resources->Init(windowHandle, DeviceResources::IF_EnableDebugLayer | DeviceResources::IF_AllowTearing);
}

void MiniApp::Update()
{
}

void MiniApp::Render()
{
	ComPtr<ID3D12CommandAllocator> allocator = m_resources->GetCommandAllocator();
	ComPtr<ID3D12GraphicsCommandList> cmdlist = m_resources->GetCommandList();
	ComPtr<ID3D12CommandQueue> cmdqueue = m_resources->GetCommandQueue();
	ComPtr<IDXGISwapChain3> swapchain = m_resources->GetSwapChain();

	allocator->Reset();
	cmdlist->Reset(allocator.Get(), nullptr);

	// Transition the render target into the correct state to allow for drawing into it.
	{
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_resources->GetCurrentRenderTarget(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		);

		cmdlist->ResourceBarrier(1, &barrier);
	}

	// Clear the backbuffer and views. 
	{
		float const blueViolet[4] = { 0.541176498f, 0.168627456f, 0.886274576f, 1.000000000f };

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptor = m_resources->GetRenderTargetView();
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvDescriptor = m_resources->GetDepthStencilView();

		cmdlist->OMSetRenderTargets(1, &rtvDescriptor, FALSE, &dsvDescriptor); // todo: do we need this?
		cmdlist->ClearRenderTargetView(rtvDescriptor, blueViolet, 0, nullptr);
		cmdlist->ClearDepthStencilView(dsvDescriptor, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	}

	// Set the viewport and scissor rect.
	{
		D3D12_VIEWPORT viewport = m_resources->GetScreenViewport();
		D3D12_RECT scissorRect = m_resources->GetScissorRect();
		cmdlist->RSSetViewports(1, &viewport);
		cmdlist->RSSetScissorRects(1, &scissorRect);
	}

	// Transition the render target to the state that allows it to be presented to the display.
	{
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_resources->GetCurrentRenderTarget(),
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

	if (m_resources->IsTearingAllowed())
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
		sprintf_s(err, "Device Lost on ResizeBuffers: Reason code 0x%08X\n", (hr == DXGI_ERROR_DEVICE_REMOVED) ? m_resources->GetD3DDevice()->GetDeviceRemovedReason() : hr);
		ASSERT_F(false, err);
	}
	else
	{
		ASSERT_RESULT(hr);
	}

	m_resources->Flush();
}

void MiniApp::Exit()
{}