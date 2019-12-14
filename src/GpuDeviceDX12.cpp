#include "GpuDeviceDX12.h"
#include "Core.h"

void GpuDeviceDX12::BeginPresent()
{
	ComPtr<ID3D12CommandAllocator> allocator = GetCommandAllocator();
	ComPtr<ID3D12GraphicsCommandList> cmdlist = GetCommandList();
	ComPtr<ID3D12CommandQueue> cmdqueue = GetCommandQueue();
	ComPtr<IDXGISwapChain3> swapchain = GetSwapChain();

	HRESULT hr = allocator->Reset();
	ASSERT_RESULT(hr);

	hr = cmdlist->Reset(allocator.Get(), nullptr);
	ASSERT_RESULT(hr);

	// Transition the render target into the correct state to allow for drawing into it.
	TransitionBarrier(GetCurrentRenderTarget(), ResourceState::RS_PRESENT, ResourceState::RS_RENDER_TARGET);

	// Clear the backbuffer and views. 
	{
		f32 const blueViolet[4] = { 0.541176498f, 0.168627456f, 0.886274576f, 1.000000000f };

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptor = GetRenderTargetView();
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvDescriptor = GetDepthStencilView();

		cmdlist->OMSetRenderTargets(1, &rtvDescriptor, FALSE, &dsvDescriptor); // todo: do we need this?
		cmdlist->ClearRenderTargetView(rtvDescriptor, blueViolet, 0, nullptr);
		cmdlist->ClearDepthStencilView(dsvDescriptor, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	}

	// Set the viewport and scissor rect.
	{
		D3D12_VIEWPORT viewport = GetScreenViewport();
		D3D12_RECT scissorRect = GetScissorRect();
		cmdlist->RSSetViewports(1, &viewport);
		cmdlist->RSSetScissorRects(1, &scissorRect);
	}

	// Transition the render target to the state that allows it to be presented to the display.
	TransitionBarrier(GetCurrentRenderTarget(), ResourceState::RS_RENDER_TARGET, ResourceState::RS_PRESENT);
}

void GpuDeviceDX12::EndPresent()
{
	ComPtr<ID3D12GraphicsCommandList> cmdlist = GetCommandList();
	ComPtr<ID3D12CommandQueue> cmdqueue = GetCommandQueue();
	ComPtr<IDXGISwapChain3> swapchain = GetSwapChain();

	// Send the command list off to the GPU for processing.
	HRESULT hr = cmdlist->Close();
	ASSERT_RESULT(hr);

	ID3D12CommandList* ppCommandLists[] = { cmdlist.Get() };
	cmdqueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	if (IsTearingAllowed())
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
		HRESULT removedReason = (hr == DXGI_ERROR_DEVICE_REMOVED) ? GetD3DDevice()->GetDeviceRemovedReason() : hr;
		ASSERT_FAIL_F("Device Lost on ResizeBuffers: Reason code 0x%08X\n", removedReason);

	}
	else
	{
		ASSERT_RESULT(hr);
	}

	Flush();
}

void GpuDeviceDX12::WaitForFenceValue(ID3D12Fence* fence, uint64_t fenceValue, HANDLE fenceEvent, u32 durationMS)
{
	// TODO(pgPW): Add hang detection here. Set timer, wait interval, if not completed, assert.

	if (fence->GetCompletedValue() < fenceValue)
	{
		HRESULT hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
		ASSERT_RESULT(hr);
		WaitForSingleObjectEx(fenceEvent, durationMS, FALSE);
	}
}

u64 GpuDeviceDX12::Signal(ID3D12CommandQueue* commandQueue, ID3D12Fence* fence, u64 fenceValue)
{	
	HRESULT hr = commandQueue->Signal(fence, fenceValue);
	ASSERT_RESULT(hr);

	return fenceValue + 1;
}

void GpuDeviceDX12::Flush()
{
	// Prepare to render the next frame.
	ID3D12Fence* fence = GetFence();
	u64 currentFenceValue = GetCurrentFenceValue();
	u64 nextFenceValue = Signal(GetCommandQueue(), fence, currentFenceValue);

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	WaitForFenceValue(fence, currentFenceValue, GetFenceEvent());

	// Set the fence value for the next frame.
	m_fenceValue = nextFenceValue;

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	if (!m_dxgiFactory->IsCurrent())
	{
		// Output information is cached on the DXGI Factory. If it is stale we need to create a new factory.
		HRESULT hr = CreateDXGIFactory2(m_dxgiFactoryFlags, IID_PPV_ARGS(m_dxgiFactory.ReleaseAndGetAddressOf()));
		ASSERT_RESULT(hr);
	}
}

static D3D12_RESOURCE_STATES ResourceStateToDX12(ResourceState::Enum state)
{
	return static_cast<D3D12_RESOURCE_STATES>(state);
}

void GpuDeviceDX12::TransitionBarrier(ID3D12Resource* resources, ResourceState::Enum stateBefore, ResourceState::Enum stateAfter)
{
	ASSERT(resources);

	D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		resources,
		ResourceStateToDX12(stateBefore),
		ResourceStateToDX12(stateAfter)
	);
	
	GetCommandList()->ResourceBarrier(1, &barrier);
}

void GpuDeviceDX12::TransitionBarriers(ID3D12Resource** resources, u8 numBarriers, ResourceState::Enum stateBefore, ResourceState::Enum stateAfter)
{
	ASSERT(resources);

	D3D12_RESOURCE_BARRIER barriers[256];
	for (u8 i = 0; i < numBarriers; ++i)
	{
		ASSERT(resources[i]);

		barriers[i].Transition.pResource = resources[i];
		barriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[i].Transition.StateAfter = ResourceStateToDX12(stateAfter);
		barriers[i].Transition.StateBefore = ResourceStateToDX12(stateBefore);
		barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	}

	GetCommandList()->ResourceBarrier(numBarriers, barriers);
}

CD3DX12_CPU_DESCRIPTOR_HANDLE GpuDeviceDX12::GetRenderTargetView() const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
}

CD3DX12_CPU_DESCRIPTOR_HANDLE GpuDeviceDX12::GetDepthStencilView() const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
}

bool GpuDeviceDX12::IsTearingAllowed()
{
	return m_initFlags & IF_AllowTearing;
}

// This method acquires the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, try WARP.
IDXGIAdapter1* getFirstAvailableHardwareAdapter(ComPtr<IDXGIFactory4> dxgiFactory, D3D_FEATURE_LEVEL minFeatureLevel)
{
	ComPtr<IDXGIAdapter1> adapter;

	u32 adapterIndex = 0;
	HRESULT getAdapterResult = S_OK;

	while (getAdapterResult != DXGI_ERROR_NOT_FOUND)
	{
		getAdapterResult = dxgiFactory->EnumAdapters1(adapterIndex, adapter.ReleaseAndGetAddressOf());


		DXGI_ADAPTER_DESC1 desc = {};
		HRESULT hr = adapter->GetDesc1(&desc);
		ASSERT_RESULT(hr);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// Don't select the Basic Render Driver adapter.
			continue;
		}

		// Check to see if the adapter supports Direct3D 12, but don't create the actual device yet.
		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), minFeatureLevel, _uuidof(ID3D12Device), nullptr)))
		{
			LOG(Log::GfxDevice, "Direct3D Adapter (id: %u) found");
			LOG(Log::GfxDevice, "Description: %ls", desc.Description);
			LOG(Log::GfxDevice, "Vendor ID: %u", desc.VendorId);
			LOG(Log::GfxDevice, "Device ID: %u", desc.DeviceId);
			LOG(Log::GfxDevice, "SubSys ID: %u", desc.SubSysId);
			LOG(Log::GfxDevice, "Revision: %u", desc.Revision);
			LOG(Log::GfxDevice, "Dedicated Video Memory: %llu MB", BytesToMegaBytes(desc.DedicatedVideoMemory));
			LOG(Log::GfxDevice, "Dedicated System Memory: %llu MB", BytesToMegaBytes(desc.DedicatedSystemMemory));
			LOG(Log::GfxDevice, "Shared System Memory: %llu MB", BytesToMegaBytes(desc.SharedSystemMemory));
			break;
		}

		adapterIndex++;
	}

	if (!adapter)
	{
		// Try WARP12 instead
		if (FAILED(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()))))
		{
			ASSERT_F(false, "WARP12 not available. Enable the 'Graphics Tools' optional feature");
		}

		LOG(Log::GfxDevice, "Direct3D Adapter - WARP12\n");
	}

	ASSERT_F(adapter != nullptr, "No Direct3D 12 device found");
	return adapter.Detach();
}

void GpuDeviceDX12::Init(void* windowHandle, u32 initFlags)
{
	const bool bEnableDebugLayer = initFlags & IF_EnableDebugLayer;
	const bool bWantAllowTearing = initFlags & IF_AllowTearing;
	bool bAllowTearing = bWantAllowTearing;

	m_window = static_cast<HWND>(windowHandle);

	m_d3dMinFeatureLevel = D3D_FEATURE_LEVEL_11_0;
	m_d3dFeatureLevel = D3D_FEATURE_LEVEL_11_0;

	m_frameIndex = 0;
	m_backBufferCount = 2;

	m_rtvDescriptorSize = 0;
	m_backBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
	m_depthBufferFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	m_dxgiFactoryFlags = 0;
	m_outputSize = { 0, 0, 1, 1 };
	m_colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

	m_fenceValue = 0;

	if (bEnableDebugLayer)
	{
		enableDebugLayer();
	}

	HRESULT createFactoryResult = CreateDXGIFactory2(m_dxgiFactoryFlags, IID_PPV_ARGS(m_dxgiFactory.ReleaseAndGetAddressOf()));
	ASSERT_RESULT(createFactoryResult);

	if (bWantAllowTearing)
	{
		bAllowTearing = checkTearingSupport();

		if (!bAllowTearing)
		{
			initFlags &= ~IF_AllowTearing;
		}
	}

	m_initFlags = initFlags;

	createDevice(bEnableDebugLayer);
	checkFeatureLevel();
	createCommandQueue();
	createDescriptorHeaps();
	createCommandAllocators();
	createCommandList();
	createEndOfFrameFence();

	initWindowSizeDependent();
}

DXGI_FORMAT formatSrgbToLinear(DXGI_FORMAT fmt)
{
	switch (fmt)
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:   return DXGI_FORMAT_R8G8B8A8_UNORM;
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:   return DXGI_FORMAT_B8G8R8A8_UNORM;
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:   return DXGI_FORMAT_B8G8R8X8_UNORM;
	default:                                return fmt;
	}
}

void GpuDeviceDX12::resizeSwapChain(u32 width, u32 height, DXGI_FORMAT format)
{
	HRESULT hr = m_swapChain->ResizeBuffers(
		m_backBufferCount,
		width,
		height,
		format,
		(m_initFlags & IF_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0
	);

	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
		char err[64] = {};
		sprintf_s(err, "Device Lost on ResizeBuffers: Reason code 0x%08X\n", (hr == DXGI_ERROR_DEVICE_REMOVED) ? m_d3dDevice->GetDeviceRemovedReason() : hr);

		ASSERT_F(false, err);
	}
}

void GpuDeviceDX12::createSwapChain(u32 width, u32 height, DXGI_FORMAT format)
{

	// Create a descriptor for the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = width;
	swapChainDesc.Height = height;
	swapChainDesc.Format = format;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = m_backBufferCount;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	swapChainDesc.Flags = (m_initFlags & IF_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
	fsSwapChainDesc.Windowed = TRUE;

	ComPtr<IDXGISwapChain1> swapChain;

	HRESULT hr = m_dxgiFactory->CreateSwapChainForHwnd(
		m_commandQueue.Get(),
		m_window,
		&swapChainDesc,
		&fsSwapChainDesc,
		nullptr,
		swapChain.GetAddressOf()
	);

	ASSERT_RESULT(hr);

	hr = swapChain.As(&m_swapChain);
	ASSERT_RESULT(hr);

	// This class does not support exclusive full-screen mode and prevents DXGI from responding to the ALT+ENTER shortcut
	hr = m_dxgiFactory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER);
	ASSERT_RESULT(hr);
}

void GpuDeviceDX12::updateColorSpace()
{
	DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	bool bIsDisplayHDR10 = false;
	HRESULT hr = S_OK;

#if USES_DXGI6
	ASSERT(m_swapChain);

	ComPtr<IDXGIOutput> output;
	hr = m_swapChain->GetContainingOutput(output.GetAddressOf());
	ASSERT_RESULT(hr);

	ComPtr<IDXGIOutput6> output6;
	hr = output.As(&output6);
	ASSERT_RESULT(hr);

	DXGI_OUTPUT_DESC1 desc = {};
	hr = output6->GetDesc1(&desc);
	ASSERT_RESULT(hr);

	bIsDisplayHDR10 = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
#endif

	if ((m_initFlags & InitFlags::IF_EnableHDR) && bIsDisplayHDR10)
	{
		switch (m_backBufferFormat)
		{
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		{
			// The application creates the HDR10 signal.
			colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
			break;
		}
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		{
			// The system creates the HDR10 signal; application uses linear values.
			colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
			break;
		}
		default:
		{
			// Not sure if this is a valid case.
			ASSERT(false);
			break;
		}
		}
	}

	m_colorSpace = colorSpace;

	u32 colorSpaceSupport = 0;
	hr = m_swapChain->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport);

	if (SUCCEEDED(hr) && (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
	{
		hr = m_swapChain->SetColorSpace1(colorSpace);
		ASSERT_RESULT(hr);
	}

}

void GpuDeviceDX12::initWindowSizeDependent()
{
	ASSERT(m_window);

	// Release resources that are tied to the swap chain and update fence values.
	for (u32 n = 0; n < m_backBufferCount; n++)
	{
		m_renderTargets[n].Reset();
	}

	const u32 backBufferWidth = max(static_cast<u32>(m_outputSize.right - m_outputSize.left), 1u);
	const u32 backBufferHeight = max(static_cast<u32>(m_outputSize.bottom - m_outputSize.top), 1u);
	const DXGI_FORMAT backBufferFormat = formatSrgbToLinear(m_backBufferFormat);

	// If the swap chain already exists, resize it, otherwise create one.
	if (m_swapChain)
	{
		resizeSwapChain(backBufferWidth, backBufferHeight, backBufferFormat);
	}
	else
	{
		createSwapChain(backBufferWidth, backBufferHeight, backBufferFormat);
	}

	updateColorSpace();

	createBackBuffers();

	// Reset the index to the current back buffer.
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	if (m_depthBufferFormat != DXGI_FORMAT_UNKNOWN)
	{
		createDepthBuffer(backBufferWidth, backBufferHeight);
	}

	// Set the 3D rendering viewport and scissor rectangle to target the entire window.
	m_screenViewport.TopLeftX = m_screenViewport.TopLeftY = 0.f;
	m_screenViewport.Width = static_cast<f32>(backBufferWidth);
	m_screenViewport.Height = static_cast<f32>(backBufferHeight);
	m_screenViewport.MinDepth = D3D12_MIN_DEPTH;
	m_screenViewport.MaxDepth = D3D12_MAX_DEPTH;

	m_scissorRect.left = m_scissorRect.top = 0;
	m_scissorRect.right = backBufferWidth;
	m_scissorRect.bottom = backBufferHeight;
}

void GpuDeviceDX12::createBackBuffers()
{
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = m_backBufferFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	for (u32 i = 0; i < m_backBufferCount; i++)
	{
		HRESULT hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(m_renderTargets[i].GetAddressOf()));
		ASSERT_RESULT(hr);

		wchar_t name[25] = {};
		swprintf_s(name, L"Render target %d", i);
		m_renderTargets[i]->SetName(name);

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptor(m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), i, static_cast<UINT>(m_rtvDescriptorSize));
		m_d3dDevice->CreateRenderTargetView(m_renderTargets[i].Get(), &rtvDesc, rtvDescriptor);
	}
}

void GpuDeviceDX12::createDepthBuffer(u32 width, u32 height)
{

	// Allocate a 2-D surface as the depth/stencil buffer and create a depth/stencil view
	// on this surface.
	CD3DX12_HEAP_PROPERTIES depthHeapProperties(D3D12_HEAP_TYPE_DEFAULT);

	D3D12_RESOURCE_DESC depthStencilDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		m_depthBufferFormat,
		width,
		height,
		1, // This depth stencil view has only one texture.
		1  // Use a single mipmap level.
	);
	depthStencilDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
	depthOptimizedClearValue.Format = m_depthBufferFormat;
	depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
	depthOptimizedClearValue.DepthStencil.Stencil = 0;

	HRESULT hr = m_d3dDevice->CreateCommittedResource(
		&depthHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthOptimizedClearValue,
		IID_PPV_ARGS(m_depthStencil.ReleaseAndGetAddressOf())
	);

	ASSERT_RESULT(hr);

	m_depthStencil->SetName(L"Depth stencil");

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = m_depthBufferFormat;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	m_d3dDevice->CreateDepthStencilView(m_depthStencil.Get(), &dsvDesc, m_dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
}

void GpuDeviceDX12::enableDebugLayer()
{
	ComPtr<ID3D12Debug> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf()))))
	{
		debugController->EnableDebugLayer();
	}
	else
	{
		LOG(Log::GfxDevice, "WARNING: Direct3D Debug Device is not available\n");
	}

	ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(dxgiInfoQueue.GetAddressOf()))))
	{
		m_dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;

		dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
		dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
	}
}

bool GpuDeviceDX12::checkTearingSupport()
{
	BOOL allowTearing = FALSE;
	ComPtr<IDXGIFactory5> factory5;
	HRESULT hr = m_dxgiFactory.As(&factory5);

	if (SUCCEEDED(hr))
	{
		hr = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
	}

	bool bAllowTearing = SUCCEEDED(hr) && allowTearing;

	if (!bAllowTearing)
	{
		LOG(Log::GfxDevice, "Variable refresh rate displays not supported");
	}

	return bAllowTearing;
}

void GpuDeviceDX12::createDevice(bool bEnableDebugLayer)
{
	ComPtr<IDXGIAdapter1> adapter;
	*adapter.GetAddressOf() = getFirstAvailableHardwareAdapter(m_dxgiFactory, m_d3dMinFeatureLevel);

	D3D12CreateDevice(
		adapter.Get(),
		m_d3dMinFeatureLevel,
		IID_PPV_ARGS(m_d3dDevice.ReleaseAndGetAddressOf())
	);

	m_d3dDevice->SetName(L"DeviceResources");

	// Configure debug device (if active).
	ComPtr<ID3D12InfoQueue> d3dInfoQueue;
	if (SUCCEEDED(m_d3dDevice.As(&d3dInfoQueue)))
	{
		if (bEnableDebugLayer)
		{
			d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
			d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		}

		D3D12_MESSAGE_ID hide[] =
		{
			D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
			D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE
		};

		D3D12_INFO_QUEUE_FILTER filter = {};
		filter.DenyList.NumIDs = _countof(hide);
		filter.DenyList.pIDList = hide;
		d3dInfoQueue->AddStorageFilterEntries(&filter);
	}
}

void GpuDeviceDX12::checkFeatureLevel()
{
	// Determine maximum supported feature level for this device
	static const D3D_FEATURE_LEVEL s_featureLevels[] =
	{
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};

	D3D12_FEATURE_DATA_FEATURE_LEVELS featLevels =
	{
		_countof(s_featureLevels), s_featureLevels, D3D_FEATURE_LEVEL_11_0
	};

	HRESULT hr = m_d3dDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featLevels, sizeof(featLevels));
	if (SUCCEEDED(hr))
	{
		m_d3dFeatureLevel = featLevels.MaxSupportedFeatureLevel;
	}
	else
	{
		m_d3dFeatureLevel = m_d3dMinFeatureLevel;
	}
}

void GpuDeviceDX12::createCommandQueue()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	HRESULT queueCreated = m_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(m_commandQueue.ReleaseAndGetAddressOf()));
	ASSERT_RESULT(queueCreated);

	m_commandQueue->SetName(L"DeviceResources");
}

void GpuDeviceDX12::createDescriptorHeaps()
{
	// Render targets
	D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {};
	rtvDescriptorHeapDesc.NumDescriptors = m_backBufferCount;
	rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

	HRESULT descrHeapCreated = m_d3dDevice->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(m_rtvDescriptorHeap.ReleaseAndGetAddressOf()));
	ASSERT_RESULT(descrHeapCreated);

	m_rtvDescriptorHeap->SetName(L"DeviceResources");
	m_rtvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// Depth Stencil Views
	ASSERT(m_depthBufferFormat != DXGI_FORMAT_UNKNOWN);

	D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc = {};
	dsvDescriptorHeapDesc.NumDescriptors = 1;
	dsvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

	descrHeapCreated = m_d3dDevice->CreateDescriptorHeap(&dsvDescriptorHeapDesc, IID_PPV_ARGS(m_dsvDescriptorHeap.ReleaseAndGetAddressOf()));

	m_dsvDescriptorHeap->SetName(L"DeviceResources");
}

void GpuDeviceDX12::createCommandAllocators()
{
	for (u32 n = 0; n < m_backBufferCount; n++)
	{
		HRESULT createdAllocator = m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_commandAllocators[n].ReleaseAndGetAddressOf()));
		ASSERT_RESULT(createdAllocator);

		wchar_t name[25] = {};
		swprintf_s(name, L"Render target %u", n);
		m_commandAllocators[n]->SetName(name);
	}
}

void GpuDeviceDX12::createCommandList()
{
	HRESULT cmdListCreated = m_d3dDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_commandAllocators[0].Get(),
		nullptr,
		IID_PPV_ARGS(m_commandList.ReleaseAndGetAddressOf()));

	ASSERT_RESULT(cmdListCreated);
	HRESULT closed = m_commandList->Close();
	ASSERT_RESULT(closed);

	m_commandList->SetName(L"DeviceResources");
}

void GpuDeviceDX12::createEndOfFrameFence()
{
	// Create a fence for tracking GPU execution progress.
	HRESULT createdFence = m_d3dDevice->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.ReleaseAndGetAddressOf()));
	ASSERT_RESULT(createdFence);
	m_fence->SetName(L"DeviceResources");

	m_fenceEvent.Attach(CreateEvent(nullptr, FALSE, FALSE, nullptr));
	ASSERT(m_fenceEvent.IsValid());
}