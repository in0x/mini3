#include "GpuDeviceDX12.h"
#include "Core.h"

static constexpr size_t GPU_SAMPLER_HEAP_COUNT = 16;
static constexpr size_t GPU_RESOURCE_HEAP_CBV_COUNT = 12;
static constexpr size_t GPU_RESOURCE_HEAP_SRV_COUNT = 64;
static constexpr size_t GPU_RESOURCE_HEAP_UAV_COUNT = 8;
static constexpr size_t GPU_RESOURCE_HEAP_CBV_SRV_UAV_COUNT = GPU_RESOURCE_HEAP_CBV_COUNT + GPU_RESOURCE_HEAP_SRV_COUNT + GPU_RESOURCE_HEAP_UAV_COUNT;

size_t DescriptorTableFrameAllocator::GetBoundDescriptorHeapSize() const
{
	return ShaderStage::Count * m_item_count;
}

DescriptorTableFrameAllocator::DescriptorTableFrameAllocator()
{
}

DescriptorTableFrameAllocator::~DescriptorTableFrameAllocator()
{
	ASSERT(m_bound_descriptors == nullptr);
}

void DescriptorTableFrameAllocator::Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 max_rename_count)
{
	if (type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
	{
		m_item_count = GPU_SAMPLER_HEAP_COUNT;
	}
	else if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
	{
		m_item_count = GPU_RESOURCE_HEAP_CBV_SRV_UAV_COUNT;
	}
	else
	{
		ASSERT_FAIL_F("Invalid Descriptor Heap Type provided");
		m_item_count = 0;
	}

	D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
	heap_desc.NodeMask = 0;
	heap_desc.Type = type;
	heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	heap_desc.NumDescriptors = m_item_count * ShaderStage::Count;

	HRESULT hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_heap_cpu));
	ASSERT_RESULT(hr);

	heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heap_desc.NumDescriptors = m_item_count * ShaderStage::Count * max_rename_count;

	hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_heap_gpu));
	ASSERT_RESULT(hr);

	m_descriptor_type = type;
	m_item_size = device->GetDescriptorHandleIncrementSize(type);
	m_bound_descriptors = new D3D12_CPU_DESCRIPTOR_HANDLE const*[GetBoundDescriptorHeapSize()];
}

void DescriptorTableFrameAllocator::Destroy()
{
	delete m_bound_descriptors;
	m_bound_descriptors = nullptr;
}


void DescriptorTableFrameAllocator::Reset(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE* null_descriptors_sampler_cbv_srv_uav)
{
	memzero(m_bound_descriptors, GetBoundDescriptorHeapSize() * sizeof(D3D12_CPU_DESCRIPTOR_HANDLE*));
	m_ring_offset = 0;

	for (u32 stage = 0; stage < ShaderStage::Count; ++stage)
	{
		m_is_dirty[stage] = true;

		D3D12_CPU_DESCRIPTOR_HANDLE dst_staging = m_heap_cpu->GetCPUDescriptorHandleForHeapStart();
		size_t const dst_base_ptr = dst_staging.ptr;

		if (m_descriptor_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
		{
			for (u32 slot = 0; slot < GPU_RESOURCE_HEAP_CBV_SRV_UAV_COUNT; ++slot)
			{
				dst_staging.ptr = dst_base_ptr + (stage * m_item_count + slot) * m_item_size;
				device->CopyDescriptorsSimple(1, dst_staging, null_descriptors_sampler_cbv_srv_uav[1], m_descriptor_type);
			}

			for (int slot = 0; slot < GPU_RESOURCE_HEAP_SRV_COUNT; ++slot)
			{
				dst_staging.ptr = dst_base_ptr + (stage * m_item_count + GPU_RESOURCE_HEAP_CBV_COUNT + slot) * m_item_size;
				device->CopyDescriptorsSimple(1, dst_staging, null_descriptors_sampler_cbv_srv_uav[2], m_descriptor_type);
			}

			for (int slot = 0; slot < GPU_RESOURCE_HEAP_UAV_COUNT; ++slot)
			{
				dst_staging.ptr = dst_base_ptr + (stage * m_item_count + GPU_RESOURCE_HEAP_CBV_COUNT + GPU_RESOURCE_HEAP_SRV_COUNT + slot) * m_item_size;
				device->CopyDescriptorsSimple(1, dst_staging, null_descriptors_sampler_cbv_srv_uav[3], m_descriptor_type);
			}
		}
		else
		{
			for (int slot = 0; slot < GPU_SAMPLER_HEAP_COUNT; ++slot)
			{
				dst_staging.ptr = dst_base_ptr + (stage * m_item_count + slot) * m_item_size;
				device->CopyDescriptorsSimple(1, dst_staging, null_descriptors_sampler_cbv_srv_uav[0], m_descriptor_type);
			}
		}
	}
}

void DescriptorTableFrameAllocator::BindDescriptor(ShaderStage::Enum stage, u32 offset, D3D12_CPU_DESCRIPTOR_HANDLE const* descriptor, ID3D12Device* device, ID3D12GraphicsCommandList* command_list)
{
	u32 index = stage * m_item_count + offset;
	if (m_bound_descriptors[index] == descriptor)
	{
		return;
	}

	m_bound_descriptors[index] = descriptor;
	m_is_dirty[stage] = true;

	D3D12_CPU_DESCRIPTOR_HANDLE dst_staging = m_heap_cpu->GetCPUDescriptorHandleForHeapStart();
	dst_staging.ptr = index * m_item_size;
	device->CopyDescriptorsSimple(1, dst_staging, *descriptor, m_descriptor_type);
}

void DescriptorTableFrameAllocator::Update(ID3D12Device* device, ID3D12GraphicsCommandList* command_list)
{
	for (u32 stage = 0; stage < ShaderStage::Count; ++stage)
	{
		if (!m_is_dirty[stage])
		{
			continue;
		}

		// Copy table contents over.
		D3D12_CPU_DESCRIPTOR_HANDLE dst = m_heap_gpu->GetCPUDescriptorHandleForHeapStart();
		dst.ptr += m_ring_offset;

		D3D12_CPU_DESCRIPTOR_HANDLE src = m_heap_cpu->GetCPUDescriptorHandleForHeapStart();
		src.ptr += (stage * m_item_count) * m_item_size;
		
		device->CopyDescriptorsSimple(m_item_count, dst, src, m_descriptor_type);

		// Bind the table to the root signature.
		D3D12_GPU_DESCRIPTOR_HANDLE table = m_heap_gpu->GetGPUDescriptorHandleForHeapStart();

		if (stage == ShaderStage::Compute)
		{
			if (m_descriptor_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			{
				command_list->SetComputeRootDescriptorTable(0, table);
			}
			else
			{
				command_list->SetComputeRootDescriptorTable(1, table);
			}
		}
		else
		{
			if (m_descriptor_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			{
				command_list->SetGraphicsRootDescriptorTable(stage * 2 + 0, table);
			}
			else
			{
				command_list->SetGraphicsRootDescriptorTable(stage * 2 + 1, table);
			}
		}

		m_is_dirty[stage] = false;
		m_ring_offset += m_item_count * m_item_size;
	}
}

void TransientFrameResourceAllocator::Create(ID3D12Device* device, size_t size_bytes)
{
	CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(size_bytes);

	HRESULT hr = device->CreateCommittedResource(
		&heap_props,
		D3D12_HEAP_FLAG_NONE,
		&buffer_desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_resource)
	);

	ASSERT_RESULT(hr);

	// Will not perform cpu reads from the resource.
	void* data_ptr = nullptr;
	CD3DX12_RANGE read_range(0, 0);
	m_resource->Map(0, &read_range, &data_ptr);

	m_data_begin = static_cast<u8*>(data_ptr);
	m_data_current = m_data_begin;
	m_data_end = m_data_begin + size_bytes;
}

u8* TransientFrameResourceAllocator::Allocate(size_t size_bytes, size_t alignment)
{
	ptrdiff_t alignment_padding = GetAlignmentAdjustment(m_data_current, alignment);
	ASSERT((m_data_current + size_bytes + alignment_padding) <= m_data_end);

	u8* allocation = AlignAddress(m_data_current, alignment);
	m_data_current += (size_bytes + alignment_padding);

	return allocation;
}

u64 TransientFrameResourceAllocator::CalculateOffset(u8* address)
{
	ASSERT(address >= m_data_begin && address < m_data_end);
	return static_cast<u64>(address - m_data_begin);
}

void TransientFrameResourceAllocator::Clear()
{
}

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
	TransitionBarrier(GetCurrentRenderTarget(), ResourceState::Present, ResourceState::Render_Target);

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
	TransitionBarrier(GetCurrentRenderTarget(), ResourceState::Render_Target, ResourceState::Present);
}

void GpuDeviceDX12::EndPresent()
{
	// TODO (update the root signature bindginds here )

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
	m_fence_value = nextFenceValue;

	m_frame_index = m_swap_chain->GetCurrentBackBufferIndex();

	if (!m_dxgi_factory->IsCurrent())
	{
		// Output information is cached on the DXGI Factory. If it is stale we need to create a new factory.
		HRESULT hr = CreateDXGIFactory2(m_dxgi_factory_flags, IID_PPV_ARGS(m_dxgi_factory.ReleaseAndGetAddressOf()));
		ASSERT_RESULT(hr);
	}
}

static D3D12_RESOURCE_STATES ResourceStateToDX12(ResourceState::Enum state)
{
	return static_cast<D3D12_RESOURCE_STATES>(state);
}

void GpuDeviceDX12::TransitionBarrier(ID3D12Resource* resources, ResourceState::Enum state_before, ResourceState::Enum state_after)
{
	ASSERT(resources);

	D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		resources,
		ResourceStateToDX12(state_before),
		ResourceStateToDX12(state_after)
	);
	
	GetCommandList()->ResourceBarrier(1, &barrier);
}

void GpuDeviceDX12::TransitionBarriers(ID3D12Resource** resources, u8 num_barriers, ResourceState::Enum state_before, ResourceState::Enum state_after)
{
	ASSERT(resources);

	D3D12_RESOURCE_BARRIER barriers[256];
	for (u8 i = 0; i < num_barriers; ++i)
	{
		ASSERT(resources[i]);

		barriers[i].Transition.pResource = resources[i];
		barriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barriers[i].Transition.StateAfter = ResourceStateToDX12(state_after);
		barriers[i].Transition.StateBefore = ResourceStateToDX12(state_before);
		barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	}

	GetCommandList()->ResourceBarrier(num_barriers, barriers);
}

CD3DX12_CPU_DESCRIPTOR_HANDLE GpuDeviceDX12::GetRenderTargetView() const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart(), m_frame_index, m_rtv_descriptor_size);
}

CD3DX12_CPU_DESCRIPTOR_HANDLE GpuDeviceDX12::GetDepthStencilView() const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_dsv_descriptor_heap->GetCPUDescriptorHandleForHeapStart());
}

bool GpuDeviceDX12::IsTearingAllowed()
{
	return m_init_flags & InitFlags::Allow_Tearing;
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
#ifdef _DEBUG
	const bool bEnableDebugLayer = initFlags & InitFlags::Enabled_Debug_Layer;
#else
	const bool bEnableDebugLayer = false;
#endif
	const bool bWantAllowTearing = initFlags & InitFlags::Allow_Tearing;
	bool bAllowTearing = bWantAllowTearing;

	m_window = static_cast<HWND>(windowHandle);

	m_d3d_min_feature_level = D3D_FEATURE_LEVEL_11_0;
	m_d3d_feature_level = D3D_FEATURE_LEVEL_11_0;

	m_frame_index = 0;
	m_backbuffer_count = 2;

	m_rtv_descriptor_size = 0;
	m_backbuffer_format = DXGI_FORMAT_B8G8R8A8_UNORM;
	m_depthbuffer_format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	m_dxgi_factory_flags = 0;
	m_output_size = { 0, 0, 1, 1 };
	m_color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

	m_fence_value = 0;

	if (bEnableDebugLayer)
	{
		enableDebugLayer();
	}

	HRESULT createFactoryResult = CreateDXGIFactory2(m_dxgi_factory_flags, IID_PPV_ARGS(m_dxgi_factory.ReleaseAndGetAddressOf()));
	ASSERT_RESULT(createFactoryResult);

	if (bWantAllowTearing)
	{
		bAllowTearing = checkTearingSupport();

		if (!bAllowTearing)
		{
			initFlags &= ~InitFlags::Allow_Tearing;
		}
	}

	m_init_flags = initFlags;

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
	HRESULT hr = m_swap_chain->ResizeBuffers(
		m_backbuffer_count,
		width,
		height,
		format,
		(m_init_flags & InitFlags::Allow_Tearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0
	);

	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
		char err[64] = {};
		sprintf_s(err, "Device Lost on ResizeBuffers: Reason code 0x%08X\n", (hr == DXGI_ERROR_DEVICE_REMOVED) ? m_d3d_device->GetDeviceRemovedReason() : hr);

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
	swapChainDesc.BufferCount = m_backbuffer_count;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	swapChainDesc.Flags = (m_init_flags & InitFlags::Allow_Tearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
	fsSwapChainDesc.Windowed = TRUE;

	ComPtr<IDXGISwapChain1> swapChain;

	HRESULT hr = m_dxgi_factory->CreateSwapChainForHwnd(
		m_command_queue.Get(),
		m_window,
		&swapChainDesc,
		&fsSwapChainDesc,
		nullptr,
		swapChain.GetAddressOf()
	);

	ASSERT_RESULT(hr);

	hr = swapChain.As(&m_swap_chain);
	ASSERT_RESULT(hr);

	// This class does not support exclusive full-screen mode and prevents DXGI from responding to the ALT+ENTER shortcut
	hr = m_dxgi_factory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER);
	ASSERT_RESULT(hr);
}

void GpuDeviceDX12::updateColorSpace()
{
	DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
	bool bIsDisplayHDR10 = false;
	HRESULT hr = S_OK;

#if USES_DXGI6
	ASSERT(m_swap_chain);

	ComPtr<IDXGIOutput> output;
	hr = m_swap_chain->GetContainingOutput(output.GetAddressOf());
	ASSERT_RESULT(hr);

	ComPtr<IDXGIOutput6> output6;
	hr = output.As(&output6);
	ASSERT_RESULT(hr);

	DXGI_OUTPUT_DESC1 desc = {};
	hr = output6->GetDesc1(&desc);
	ASSERT_RESULT(hr);

	bIsDisplayHDR10 = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
#endif

	if ((m_init_flags & InitFlags::Enabled_HDR) && bIsDisplayHDR10)
	{
		switch (m_backbuffer_format)
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

	m_color_space = colorSpace;

	u32 colorSpaceSupport = 0;
	hr = m_swap_chain->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport);

	if (SUCCEEDED(hr) && (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
	{
		hr = m_swap_chain->SetColorSpace1(colorSpace);
		ASSERT_RESULT(hr);
	}

}

void GpuDeviceDX12::initWindowSizeDependent()
{
	ASSERT(m_window);

	// Release resources that are tied to the swap chain and update fence values.
	for (u32 n = 0; n < m_backbuffer_count; n++)
	{
		m_render_targets[n].Reset();
	}

	const u32 backBufferWidth = max(static_cast<u32>(m_output_size.right - m_output_size.left), 1u);
	const u32 backBufferHeight = max(static_cast<u32>(m_output_size.bottom - m_output_size.top), 1u);
	const DXGI_FORMAT backBufferFormat = formatSrgbToLinear(m_backbuffer_format);

	// If the swap chain already exists, resize it, otherwise create one.
	if (m_swap_chain)
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
	m_frame_index = m_swap_chain->GetCurrentBackBufferIndex();

	if (m_depthbuffer_format != DXGI_FORMAT_UNKNOWN)
	{
		createDepthBuffer(backBufferWidth, backBufferHeight);
	}

	// Set the 3D rendering viewport and scissor rectangle to target the entire window.
	m_screen_viewport.TopLeftX = m_screen_viewport.TopLeftY = 0.f;
	m_screen_viewport.Width = static_cast<f32>(backBufferWidth);
	m_screen_viewport.Height = static_cast<f32>(backBufferHeight);
	m_screen_viewport.MinDepth = D3D12_MIN_DEPTH;
	m_screen_viewport.MaxDepth = D3D12_MAX_DEPTH;

	m_scissor_rect.left = m_scissor_rect.top = 0;
	m_scissor_rect.right = backBufferWidth;
	m_scissor_rect.bottom = backBufferHeight;
}

void GpuDeviceDX12::createBackBuffers()
{
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = m_backbuffer_format;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	for (u32 i = 0; i < m_backbuffer_count; i++)
	{
		HRESULT hr = m_swap_chain->GetBuffer(i, IID_PPV_ARGS(m_render_targets[i].GetAddressOf()));
		ASSERT_RESULT(hr);

		wchar_t name[25] = {};
		swprintf_s(name, L"Render target %d", i);
		m_render_targets[i]->SetName(name);

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptor(m_rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart(), i, static_cast<UINT>(m_rtv_descriptor_size));
		m_d3d_device->CreateRenderTargetView(m_render_targets[i].Get(), &rtvDesc, rtvDescriptor);
	}
}

void GpuDeviceDX12::createDepthBuffer(u32 width, u32 height)
{

	// Allocate a 2-D surface as the depth/stencil buffer and create a depth/stencil view
	// on this surface.
	CD3DX12_HEAP_PROPERTIES depthHeapProperties(D3D12_HEAP_TYPE_DEFAULT);

	D3D12_RESOURCE_DESC depthStencilDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		m_depthbuffer_format,
		width,
		height,
		1, // This depth stencil view has only one texture.
		1  // Use a single mipmap level.
	);
	depthStencilDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
	depthOptimizedClearValue.Format = m_depthbuffer_format;
	depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
	depthOptimizedClearValue.DepthStencil.Stencil = 0;

	HRESULT hr = m_d3d_device->CreateCommittedResource(
		&depthHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthOptimizedClearValue,
		IID_PPV_ARGS(m_depth_stencil.ReleaseAndGetAddressOf())
	);

	ASSERT_RESULT(hr);

	m_depth_stencil->SetName(L"Depth stencil");

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = m_depthbuffer_format;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	m_d3d_device->CreateDepthStencilView(m_depth_stencil.Get(), &dsvDesc, m_dsv_descriptor_heap->GetCPUDescriptorHandleForHeapStart());
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
		m_dxgi_factory_flags = DXGI_CREATE_FACTORY_DEBUG;

		dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
		dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
	}
}

bool GpuDeviceDX12::checkTearingSupport()
{
	BOOL allowTearing = FALSE;
	ComPtr<IDXGIFactory5> factory5;
	HRESULT hr = m_dxgi_factory.As(&factory5);

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
	*adapter.GetAddressOf() = getFirstAvailableHardwareAdapter(m_dxgi_factory, m_d3d_min_feature_level);

	D3D12CreateDevice(
		adapter.Get(),
		m_d3d_min_feature_level,
		IID_PPV_ARGS(m_d3d_device.ReleaseAndGetAddressOf())
	);

	m_d3d_device->SetName(L"DeviceResources");

	// Configure debug device (if active).
	ComPtr<ID3D12InfoQueue> d3dInfoQueue;
	if (SUCCEEDED(m_d3d_device.As(&d3dInfoQueue)))
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

	HRESULT hr = m_d3d_device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featLevels, sizeof(featLevels));
	if (SUCCEEDED(hr))
	{
		m_d3d_feature_level = featLevels.MaxSupportedFeatureLevel;
	}
	else
	{
		m_d3d_feature_level = m_d3d_min_feature_level;
	}
}

void GpuDeviceDX12::createCommandQueue()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	HRESULT queueCreated = m_d3d_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(m_command_queue.ReleaseAndGetAddressOf()));
	ASSERT_RESULT(queueCreated);

	m_command_queue->SetName(L"DeviceResources");
}

void GpuDeviceDX12::createDescriptorHeaps()
{
	// Render targets
	D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {};
	rtvDescriptorHeapDesc.NumDescriptors = m_backbuffer_count;
	rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

	HRESULT descrHeapCreated = m_d3d_device->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(m_rtv_descriptor_heap.ReleaseAndGetAddressOf()));
	ASSERT_RESULT(descrHeapCreated);

	m_rtv_descriptor_heap->SetName(L"DeviceResources");
	m_rtv_descriptor_size = m_d3d_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// Depth Stencil Views
	ASSERT(m_depthbuffer_format != DXGI_FORMAT_UNKNOWN);

	D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc = {};
	dsvDescriptorHeapDesc.NumDescriptors = 1;
	dsvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

	descrHeapCreated = m_d3d_device->CreateDescriptorHeap(&dsvDescriptorHeapDesc, IID_PPV_ARGS(m_dsv_descriptor_heap.ReleaseAndGetAddressOf()));

	m_dsv_descriptor_heap->SetName(L"DeviceResources");
}

void GpuDeviceDX12::createCommandAllocators()
{
	for (u32 n = 0; n < m_backbuffer_count; n++)
	{
		HRESULT createdAllocator = m_d3d_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_command_allocators[n].ReleaseAndGetAddressOf()));
		ASSERT_RESULT(createdAllocator);

		wchar_t name[25] = {};
		swprintf_s(name, L"Render target %u", n);
		m_command_allocators[n]->SetName(name);
	}
}

void GpuDeviceDX12::createCommandList()
{
	HRESULT cmdListCreated = m_d3d_device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_command_allocators[0].Get(),
		nullptr,
		IID_PPV_ARGS(m_command_list.ReleaseAndGetAddressOf()));

	ASSERT_RESULT(cmdListCreated);
	HRESULT closed = m_command_list->Close();
	ASSERT_RESULT(closed);

	m_command_list->SetName(L"DeviceResources");
}

void GpuDeviceDX12::createEndOfFrameFence()
{
	// Create a fence for tracking GPU execution progress.
	HRESULT createdFence = m_d3d_device->CreateFence(m_fence_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.ReleaseAndGetAddressOf()));
	ASSERT_RESULT(createdFence);
	m_fence->SetName(L"DeviceResources");

	m_fence_event.Attach(CreateEvent(nullptr, FALSE, FALSE, nullptr));
	ASSERT(m_fence_event.IsValid());
}

void BindVertexBuffer(ID3D12GraphicsCommandList* command_list, GpuBuffer const * vertex_buffer, u8 slot, u32 strides, u32 offset)
{
	D3D12_VERTEX_BUFFER_VIEW buffer_view;

	buffer_view.BufferLocation = vertex_buffer->resource->GetGPUVirtualAddress();
	buffer_view.SizeInBytes = vertex_buffer->desc.sizes_bytes;
	buffer_view.StrideInBytes = vertex_buffer->desc.stride_in_bytes;
	buffer_view.BufferLocation += static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(offset);
	buffer_view.SizeInBytes -= offset;

	command_list->IASetVertexBuffers(static_cast<u32>(slot), 1, &buffer_view);
}

void BindVertexBuffers(ID3D12GraphicsCommandList* command_list, GpuBuffer const ** vertex_buffers, u8 slot, u8 count, u32 const* strides, u32 const* offsets)
{
	D3D12_VERTEX_BUFFER_VIEW buffer_views[256];

	for (u32 i = 0; i < count; ++i)
	{
		ASSERT(vertex_buffers[i] != nullptr);
		GpuBuffer const* buffer = vertex_buffers[i];

		buffer_views[i].BufferLocation = buffer->resource->GetGPUVirtualAddress();
		buffer_views[i].SizeInBytes = buffer->desc.sizes_bytes;
		buffer_views[i].StrideInBytes = buffer->desc.stride_in_bytes;

		if (offsets)
		{
			buffer_views[i].BufferLocation += static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(offsets[i]);
			buffer_views[i].SizeInBytes -= offsets[i];
		}
	}

	command_list->IASetVertexBuffers(static_cast<u32>(slot), static_cast<u32>(count), buffer_views);
}

void BindIndexBuffer(ID3D12GraphicsCommandList* command_list, GpuBuffer const* index_buffer, u32 offset)
{
	D3D12_INDEX_BUFFER_VIEW buffer_view;
	buffer_view.BufferLocation = index_buffer->resource->GetGPUVirtualAddress() + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(offset);
	buffer_view.SizeInBytes = index_buffer->desc.sizes_bytes;
	buffer_view.Format = index_buffer->desc.format;

	command_list->IASetIndexBuffer(&buffer_view);
}

void DrawMesh(Mesh const* mesh, ID3D12GraphicsCommandList* command_list)
{
	BindVertexBuffer(command_list, mesh->vertex_buffer_gpu, 0, 1, 0);
	BindIndexBuffer(command_list, mesh->index_buffer_gpu, 0);

	for (SubMesh const& submesh : mesh->submeshes)
	{
		command_list->DrawIndexedInstanced(submesh.num_indices, 1, submesh.first_index_location, submesh.base_vertex_location, 0);
	}
}