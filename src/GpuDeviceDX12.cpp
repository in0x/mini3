#include "GpuDeviceDX12.h"
#include "Core.h"
#include "MemUtils.h"

static constexpr size_t GPU_SAMPLER_HEAP_COUNT = 16;
static constexpr size_t GPU_RESOURCE_HEAP_CBV_COUNT = 12;
static constexpr size_t GPU_RESOURCE_HEAP_SRV_COUNT = 64;
static constexpr size_t GPU_RESOURCE_HEAP_UAV_COUNT = 8;
static constexpr size_t GPU_RESOURCE_HEAP_CBV_SRV_UAV_COUNT = GPU_RESOURCE_HEAP_CBV_COUNT + GPU_RESOURCE_HEAP_SRV_COUNT + GPU_RESOURCE_HEAP_UAV_COUNT;

size_t DescriptorTableFrameAllocator::GetBoundDescriptorHeapSize() const
{
	return ShaderStage::Count * m_item_count;
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
	ASSERT_HR(hr);

	heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heap_desc.NumDescriptors = m_item_count * ShaderStage::Count * max_rename_count;

	hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_heap_gpu));
	ASSERT_HR(hr);

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

void DescriptorTableFrameAllocator::BindDescriptor(ShaderStage::Enum stage, u32 offset, D3D12_CPU_DESCRIPTOR_HANDLE const* descriptor, ID3D12Device* device)
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

		if (stage != ShaderStage::Compute)
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
		else
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

		m_is_dirty[stage] = false;
		m_ring_offset += m_item_count * m_item_size;
	}
}

void ResourceAllocator::Create(ID3D12Device* device, size_t size_bytes)
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

	ASSERT_HR(hr);

	// Will not perform cpu reads from the resource.
	void* data_ptr = nullptr;
	CD3DX12_RANGE read_range(0, 0);
	m_resource->Map(0, &read_range, &data_ptr);

	m_data_begin = static_cast<u8*>(data_ptr);
	m_data_current = m_data_begin;
	m_data_end = m_data_begin + size_bytes;
}

u8* ResourceAllocator::Allocate(size_t size_bytes, size_t alignment)
{
	ptrdiff_t alignment_padding = Memory::GetAlignmentAdjustment(m_data_current, alignment);
	ASSERT((m_data_current + size_bytes + alignment_padding) <= m_data_end);

	u8* allocation = Memory::AlignAddress(m_data_current, alignment);
	m_data_current += (size_bytes + alignment_padding);

	return allocation;
}

u64 ResourceAllocator::CalculateOffset(u8* address)
{
	ASSERT(address >= m_data_begin && address < m_data_end);
	return static_cast<u64>(address - m_data_begin);
}

void ResourceAllocator::Clear()
{
	m_data_current = m_data_begin;
}

void DescriptorAllocator::Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 max_count)
{
	D3D12_DESCRIPTOR_HEAP_DESC desc;
	desc.NodeMask = 0;
	desc.NumDescriptors = max_count;
	desc.Type = type;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
	ASSERT_HR(hr);

	m_item_size = device->GetDescriptorHandleIncrementSize(type);
	m_item_count = 0;
	m_max_count = max_count;
	m_type = type;
}

u64 DescriptorAllocator::Allocate()
{
	ASSERT(m_item_count.load() < m_max_count);

	size_t start_ptr = m_heap->GetCPUDescriptorHandleForHeapStart().ptr;
	size_t offset = m_item_count.fetch_add(1) * m_item_size;

	return (start_ptr + offset);
}

constexpr s32 invalid_cmd_thread_id = -1;
thread_local s32 tls_cmd_allocator_idx = invalid_cmd_thread_id;
static atomic_u32 g_cmd_threads_registered = 0;

void RegisterCommandProducerThread()
{
	ASSERT(g_cmd_threads_registered.load() < NUM_MAX_THREADS);
	tls_cmd_allocator_idx = g_cmd_threads_registered++;
}

inline bool IsValidCommandProducerThread()
{
	return (tls_cmd_allocator_idx != -1);
}

inline s32 GetCmdProducerThreadId()
{
	ASSERT(IsValidCommandProducerThread());
	return tls_cmd_allocator_idx;
}

void CommandAllocator::Create(ID3D12Device* device)
{
	m_gfx_allocators = new ID3D12CommandAllocator*[NUM_MAX_THREADS];
	m_copy_allocators = new ID3D12CommandAllocator*[NUM_MAX_THREADS];
	m_compute_allocators = new ID3D12CommandAllocator*[NUM_MAX_THREADS];

	wchar_t name[64] = {};

	for (u32 thread_id = 0; thread_id < NUM_MAX_THREADS; ++thread_id)
	{
		VERIFY_HR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_gfx_allocators[thread_id])));
		VERIFY_HR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&m_copy_allocators[thread_id])));
		VERIFY_HR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&m_compute_allocators[thread_id])));

		swprintf_s(name, L"cmdallocator_gfx_%u", thread_id);
		m_gfx_allocators[thread_id]->SetName(name);

		swprintf_s(name, L"cmdallocator_copy_%u", thread_id);
		m_copy_allocators[thread_id]->SetName(name);

		swprintf_s(name, L"cmdallocator_compute_%u", thread_id);
		m_compute_allocators[thread_id]->SetName(name);
	}
}

void CommandAllocator::Destroy()
{
	delete m_gfx_allocators;
	delete m_copy_allocators;
	delete m_compute_allocators;
}

void CommandAllocator::Reset()
{
	s32 const thread_id = GetCmdProducerThreadId();

	VERIFY_HR(m_gfx_allocators[thread_id]->Reset());
	VERIFY_HR(m_copy_allocators[thread_id]->Reset());
	VERIFY_HR(m_compute_allocators[thread_id]->Reset());
}

ID3D12CommandAllocator** CommandAllocator::GetAllocatorSet(D3D12_COMMAND_LIST_TYPE type)
{
	switch (type)
	{
	case D3D12_COMMAND_LIST_TYPE_DIRECT:
		return m_gfx_allocators;
	case D3D12_COMMAND_LIST_TYPE_COMPUTE:
		return m_compute_allocators;
	case D3D12_COMMAND_LIST_TYPE_COPY:
		return m_copy_allocators;
	default:
		ASSERT_FAIL_F("Cmd allocators for this list type are not currently being created!");
		return nullptr;
	}
}

ID3D12CommandAllocator* CommandAllocator::GetAllocator(D3D12_COMMAND_LIST_TYPE type)
{
	ID3D12CommandAllocator** allocator_set = GetAllocatorSet(type);
	return allocator_set[GetCmdProducerThreadId()];
}

CommandListManager::CommandListManager()
{
	memzero(m_active_gfx_lists, sizeof(ID3D12GraphicsCommandList*) * NUM_MAX_THREADS);
	memzero(m_active_copy_lists, sizeof(ID3D12GraphicsCommandList*) * NUM_MAX_THREADS);
	memzero(m_active_compute_lists, sizeof(ID3D12GraphicsCommandList*) * NUM_MAX_THREADS);
}

ID3D12GraphicsCommandList** CommandListManager::GetListSetByType(D3D12_COMMAND_LIST_TYPE type)
{
	switch (type)
	{
	case D3D12_COMMAND_LIST_TYPE_DIRECT:
		return m_active_copy_lists;
	case D3D12_COMMAND_LIST_TYPE_COMPUTE:
		return m_active_compute_lists;
	case D3D12_COMMAND_LIST_TYPE_COPY:
		return m_active_copy_lists;
	default:
		ASSERT_FAIL();
		return nullptr;
	}
}

ID3D12GraphicsCommandList** CommandListManager::GetCommandListSlot(s32 thread_id, D3D12_COMMAND_LIST_TYPE type)
{
	ID3D12GraphicsCommandList** list_set = GetListSetByType(type);
	return &list_set[thread_id];
}

void CommandListManager::OpenCommandList(ID3D12GraphicsCommandList* cmdlist, CommandAllocator* allocator)
{
	s32 thread_id = GetCmdProducerThreadId();
	D3D12_COMMAND_LIST_TYPE list_type = cmdlist->GetType();

	ID3D12GraphicsCommandList** list_slot = GetCommandListSlot(thread_id, list_type);
	ASSERT_F(*list_slot == nullptr, "Another command list is already open on this thread!");

	ID3D12CommandAllocator* cmd_allocator = allocator->GetAllocator(list_type);

	VERIFY_HR(cmdlist->Reset(cmd_allocator, nullptr));
	*list_slot = cmdlist;
}

void CommandListManager::CloseCommandList(ID3D12GraphicsCommandList* cmdlist)
{
	s32 thread_id = GetCmdProducerThreadId();
	D3D12_COMMAND_LIST_TYPE list_type = cmdlist->GetType();

	ID3D12GraphicsCommandList** list_slot = GetCommandListSlot(thread_id, list_type);
	ASSERT_F(*list_slot == cmdlist, "Attempting to close a command list that is not open on this thread!");

	VERIFY_HR(cmdlist->Close());
	*list_slot = nullptr;
}

bool CommandListManager::EnsureAllCommandListsClosed()
{
	for (u32 i = 0; i < NUM_MAX_THREADS; ++i)
	{
		if (m_active_gfx_lists[i] != nullptr) return false;
		if (m_active_copy_lists[i] != nullptr) return false;
		if (m_active_compute_lists[i] != nullptr) return false;
	}

	return true;
}


CommandQueue::CommandQueue()
	: m_cmd_queue(nullptr)
	, m_fence(nullptr)
	, m_next_fence_value((u64)-1)
	, m_last_completed_fence_value((u64)-1)
{}

void CommandQueue::Create(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE cmd_type)
{
	m_queue_type = cmd_type;

	// We store queue_type in the upper bytes to be know
	// what queue type the fence value came from.
	m_next_fence_value = ((u64)m_queue_type << 56) + 1;
	m_last_completed_fence_value = ((u64)m_queue_type << 56);

	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = m_queue_type;
	queue_desc.NodeMask = 0;
	HRESULT hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&m_cmd_queue));
	ASSERT_HR(hr);

	hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
	ASSERT_HR(hr);

	m_fence->Signal(m_last_completed_fence_value);

	m_fence_event_handle = CreateEventEx(NULL, false, false, EVENT_ALL_ACCESS);
	ASSERT(m_fence_event_handle != INVALID_HANDLE_VALUE);
}

void CommandQueue::Release()
{
	CloseHandle(m_fence_event_handle);

	m_fence->Release();
	m_fence = nullptr;
	m_cmd_queue->Release();
	m_cmd_queue = nullptr;
}

#ifdef _DEBUG
bool CommandQueue::IsFenceFromThisQueue(u64 fence)
{
	return ((D3D12_COMMAND_LIST_TYPE)(fence >> 56) == m_queue_type);	
}
#endif // _DEBUG

bool CommandQueue::IsFenceComplete(u64 fence_value)
{
	ASSERT(IsFenceFromThisQueue(fence_value));

	if (fence_value > m_last_completed_fence_value)
	{
		FetchCurrentFenceValue();
	}

	return fence_value <= m_last_completed_fence_value;
}

void CommandQueue::InsertGpuWait(u64 fence_value)
{
	ASSERT(IsFenceFromThisQueue(fence_value));
	m_cmd_queue->Wait(m_fence.Get(), fence_value);
}

void CommandQueue::InsertGpuWaitForOtherQueue(CommandQueue* other_queue, u64 fence_value)
{
	ASSERT(IsFenceFromThisQueue(fence_value));
	m_cmd_queue->Wait(other_queue->GetFence(), fence_value);
}

void CommandQueue::InsertGpuWaitForOtherQueue(CommandQueue* other_queue)
{
	m_cmd_queue->Wait(other_queue->GetFence(), other_queue->GetNextFenceValue() - 1);
}

void CommandQueue::WaitForFenceCpuBlocking(u64 fence_value)
{
	ASSERT(IsFenceFromThisQueue(fence_value));
	if (IsFenceComplete(fence_value))
	{
		return;
	}

	ScopedLock lock(m_event_mutex);

	m_fence->SetEventOnCompletion(fence_value, m_fence_event_handle);
	WaitForSingleObjectEx(m_fence_event_handle, INFINITE, false);
	m_last_completed_fence_value = fence_value;
}

u64 CommandQueue::FetchCurrentFenceValue()
{
	m_last_completed_fence_value = max(m_last_completed_fence_value, m_fence->GetCompletedValue());
	return m_last_completed_fence_value;
}

u64 CommandQueue::ExecuteCommandList(ID3D12CommandList* cmd_list)
{
	m_cmd_queue->ExecuteCommandLists(1, &cmd_list);

	ScopedLock lock(m_fence_mutex);
	m_cmd_queue->Signal(m_fence.Get(), m_next_fence_value);

	return m_next_fence_value++;
}

u64 CommandQueue::ExecuteCommandLists(ID3D12CommandList** cmd_lists, u32 count)
{
	m_cmd_queue->ExecuteCommandLists(count, cmd_lists);

	ScopedLock lock(m_fence_mutex);
	m_cmd_queue->Signal(m_fence.Get(), m_next_fence_value);

	return m_next_fence_value++;
}

void CommandQueueManager::Create(ID3D12Device* device)
{
	m_graphics.Create(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_compute.Create(device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
	m_copy.Create(device, D3D12_COMMAND_LIST_TYPE_COPY);
}

void CommandQueueManager::Release()
{
	m_graphics.Release();
	m_compute.Release();
	m_copy.Release();
}

CommandQueue* CommandQueueManager::GetQueue(D3D12_COMMAND_LIST_TYPE type)
{
	switch (type)
	{
	case D3D12_COMMAND_LIST_TYPE_DIRECT:
		return GetGraphicsQueue();
	case D3D12_COMMAND_LIST_TYPE_COMPUTE:
		return GetComputeQueue();
	case D3D12_COMMAND_LIST_TYPE_COPY:
		return GetCopyQueue();
	default:
		ASSERT_FAIL();
		return nullptr;
	}
}

u64 CommandQueueManager::ExecuteCommandList(ID3D12CommandList* cmd_list)
{
	CommandQueue* queue = GetQueue(cmd_list->GetType());
	return queue->ExecuteCommandList(cmd_list);
}

bool CommandQueueManager::IsFenceComplete(u64 fence_value)
{
	CommandQueue* queue = GetQueue((D3D12_COMMAND_LIST_TYPE)(fence_value >> 56));
	return queue->IsFenceComplete(fence_value);
}

void CommandQueueManager::WaitForFenceCpuBlocking(u64 fence_value)
{
	CommandQueue* queue = GetQueue((D3D12_COMMAND_LIST_TYPE)(fence_value >> 56));
	queue->WaitForFenceCpuBlocking(fence_value);
}

void CommandQueueManager::WaitForAllQueuesFinished()
{
	m_graphics.WaitForQueueFinishedCpuBlocking();
	m_compute.WaitForQueueFinishedCpuBlocking();
	m_copy.WaitForQueueFinishedCpuBlocking();
}

void GpuDeviceDX12::BeginPresent()
{
	FrameResource* frame = GetFrameResources();

	// Ensure we don't have any open commandlists so we can safely reset the allocators.
	ASSERT(frame->cmd_list_manager.EnsureAllCommandListsClosed());
	frame->cmd_allocator.Reset();

	ID3D12GraphicsCommandList* cmdlist = frame->GetFrameCmdList();
	OpenCommandList(cmdlist);

	ID3D12DescriptorHeap* heaps[] = 
	{
		frame->resource_descriptors_gpu.GetGpuHeap(),
		frame->sampler_descriptors_gpu.GetGpuHeap()
	};

	cmdlist->SetDescriptorHeaps(ARRAY_SIZE(heaps), heaps);
	cmdlist->SetGraphicsRootSignature(m_graphics_root_sig.Get());
	cmdlist->SetComputeRootSignature(m_compute_root_sig.Get());

	D3D12_CPU_DESCRIPTOR_HANDLE null_descriptors[] = 
	{
		m_null_sampler,
		m_null_cbv,
		m_null_srv,
		m_null_uav
	};

	frame->resource_descriptors_gpu.Reset(GetD3DDevice(), null_descriptors);
	frame->sampler_descriptors_gpu.Reset(GetD3DDevice(), null_descriptors);
	frame->transient_resource_allocator.Clear();

	// Transition the render target into the correct state to allow for drawing into it.
	Gfx::TransitionBarrier(GetCurrentRenderTarget(), cmdlist, ResourceState::Present, ResourceState::Render_Target);

	// Set the viewport and scissor rect.
	{
		D3D12_VIEWPORT viewport = GetScreenViewport();
		D3D12_RECT scissorRect = GetScissorRect();
		cmdlist->RSSetViewports(1, &viewport);
		cmdlist->RSSetScissorRects(1, &scissorRect);
	}

	// Clear the backbuffer and views. 
	{
		f32 const blueViolet[4] = { 0.541176498f, 0.168627456f, 0.886274576f, 1.000000000f };

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptor = GetRenderTargetView();
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvDescriptor = GetDepthStencilView();

		cmdlist->OMSetRenderTargets(1, &rtvDescriptor, FALSE, &dsvDescriptor); // todo: do we need this?
		cmdlist->ClearRenderTargetView(rtvDescriptor, blueViolet, 0, nullptr);
		cmdlist->ClearDepthStencilView(dsvDescriptor, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	}	
}

void GpuDeviceDX12::EndPresent()
{
	IDXGISwapChain3* swapchain = GetSwapChain();
	ID3D12Device* device = GetD3DDevice();

	FrameResource* frame = GetFrameResources();
	ID3D12GraphicsCommandList* cmdlist = frame->GetFrameCmdList();

	frame->resource_descriptors_gpu.Update(device, cmdlist);
	frame->sampler_descriptors_gpu.Update(device, cmdlist);

	// Transition the render target to the state that allows it to be presented to the display.
	Gfx::TransitionBarrier(GetCurrentRenderTarget(), cmdlist, ResourceState::Render_Target, ResourceState::Present);

	// Send the command list off to the GPU for processing.
	CloseAndEnqueueCommandList(cmdlist);
	frame->SubmitQueuedWork(m_cmd_queue_mng.GetGraphicsQueue());

	HRESULT hr = S_OK;
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
		UNUSED(removedReason);
	}
	else
	{
		ASSERT_HR(hr);
	}

	MoveToNextFrame();
}

void GpuDeviceDX12::Flush()
{
	// Flush all outstanding work.
	FlushQueuedGraphicsWork();

	// Wait for all queues to become idle.
	m_cmd_queue_mng.WaitForAllQueuesFinished();
}

void GpuDeviceDX12::MoveToNextFrame()
{
	m_frame_index = m_swap_chain->GetCurrentBackBufferIndex();

	// If this frame has not finished rendering yet, wait until it is ready.
	m_cmd_queue_mng.GetGraphicsQueue()->WaitForFenceCpuBlocking(GetFrameResources()->end_of_frame_fence);

	if (!m_dxgi_factory->IsCurrent())
	{
		// Output information is cached on the DXGI Factory. If it is stale we need to create a new factory.
		HRESULT hr = CreateDXGIFactory2(m_dxgi_factory_flags, IID_PPV_ARGS(m_dxgi_factory.ReleaseAndGetAddressOf()));
		ASSERT_HR(hr);
	}
}

static D3D12_RESOURCE_STATES ResourceStateToDX12(ResourceState::Enum state)
{
	return static_cast<D3D12_RESOURCE_STATES>(state);
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
		ASSERT_HR(hr);

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
	const bool bEnableDebugLayer = initFlags & InitFlags::Enable_Debug_Layer;
#else
	const bool bEnableDebugLayer = false;
#endif
	const bool bWantAllowTearing = initFlags & InitFlags::Allow_Tearing;
	bool bAllowTearing = bWantAllowTearing;

	m_window = static_cast<HWND>(windowHandle);

	m_d3d_min_feature_level = D3D_FEATURE_LEVEL_11_0;
	m_d3d_feature_level = D3D_FEATURE_LEVEL_11_0;

	m_frame_index = 0;

	m_rtv_descriptor_size = 0;
	m_backbuffer_format = DXGI_FORMAT_B8G8R8A8_UNORM;
	m_depthbuffer_format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	m_dxgi_factory_flags = 0;
	m_output_size = { 0, 0, 1, 1 };
	m_color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

	if (bEnableDebugLayer)
	{
		enableDebugLayer();
	}

	HRESULT createFactoryResult = CreateDXGIFactory2(m_dxgi_factory_flags, IID_PPV_ARGS(m_dxgi_factory.ReleaseAndGetAddressOf()));
	ASSERT_HR(createFactoryResult);

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
	m_cmd_queue_mng.Create(GetD3DDevice());

	m_backbuffer_width = max(static_cast<u32>(m_output_size.right - m_output_size.left), 1u);
	m_backbuffer_height = max(static_cast<u32>(m_output_size.bottom - m_output_size.top), 1u);

	updateSwapchain(m_backbuffer_width, m_backbuffer_height);
	m_frame_index = m_swap_chain->GetCurrentBackBufferIndex();

	createDescriptorHeaps();
	createFrameResources();

	createDepthBuffer(m_backbuffer_width, m_backbuffer_height);
	UpdateViewportScissorRect(m_backbuffer_width, m_backbuffer_height);

	createGraphicsRootSig();
	createComputeRootSig();

	ID3D12Device* device = GetD3DDevice();
	m_rt_alloctor.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 128);
	m_ds_allocator.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 128);
	m_resource_allocator.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096);
	m_sampler_allocator.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 64);

	m_buffer_upload_allocator.Create(device, 256 * 1024 * 1024); // TODO: reset
	m_texture_upload_allocator.Create(device, 256 * 1024 * 1024); // TODO: reset

	createNullResources();
}

void GpuDeviceDX12::Destroy()
{
	destroyFrameResource();
}

void GpuDeviceDX12::createNullResources()
{
	{
		D3D12_SAMPLER_DESC sampler_desc = {};
		sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	
		m_null_sampler.ptr = m_sampler_allocator.Allocate();
		m_d3d_device->CreateSampler(&sampler_desc, m_null_sampler);
	}
	
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
		m_null_cbv.ptr = m_resource_allocator.Allocate();
		m_d3d_device->CreateConstantBufferView(&cbv_desc, m_null_cbv);
	}
	
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = DXGI_FORMAT_R32_UINT;
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		m_null_srv.ptr = m_resource_allocator.Allocate();
		m_d3d_device->CreateShaderResourceView(nullptr, &srv_desc, m_null_srv);
	}

	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
		uav_desc.Format = DXGI_FORMAT_R32_UINT;
		uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

		m_null_uav.ptr = m_resource_allocator.Allocate();
		m_d3d_device->CreateUnorderedAccessView(nullptr, nullptr, &uav_desc, m_null_uav);
	}
}

void GpuDeviceDX12::createGraphicsRootSig()
{
	D3D12_DESCRIPTOR_RANGE sampler_range = {};
	sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
	sampler_range.BaseShaderRegister = 0;
	sampler_range.RegisterSpace = 0;
	sampler_range.NumDescriptors = GPU_SAMPLER_HEAP_COUNT;
	sampler_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	u32 const num_descriptor_ranges = 3;
	D3D12_DESCRIPTOR_RANGE descriptor_ranges[num_descriptor_ranges];

	D3D12_DESCRIPTOR_RANGE_TYPE range_types[num_descriptor_ranges] = { D3D12_DESCRIPTOR_RANGE_TYPE_CBV , D3D12_DESCRIPTOR_RANGE_TYPE_SRV, D3D12_DESCRIPTOR_RANGE_TYPE_UAV };
	size_t range_descriptor_counts[num_descriptor_ranges] = { GPU_RESOURCE_HEAP_CBV_COUNT, GPU_RESOURCE_HEAP_SRV_COUNT, GPU_RESOURCE_HEAP_UAV_COUNT };

	for (u32 i = 0; i < num_descriptor_ranges; ++i)
	{
		descriptor_ranges[i].RangeType = range_types[i];
		descriptor_ranges[i].BaseShaderRegister = 0;
		descriptor_ranges[i].RegisterSpace = 0;
		descriptor_ranges[i].NumDescriptors = (u32)range_descriptor_counts[i];
		descriptor_ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	}

	u32 const num_params = 2 * (ShaderStage::Count - 1); // 2: resource,sampler; 5: vs,hs,ds,gs,ps;
	D3D12_ROOT_PARAMETER params[num_params];

	D3D12_SHADER_VISIBILITY visibilities[num_params] = 
	{
		D3D12_SHADER_VISIBILITY_VERTEX,
		D3D12_SHADER_VISIBILITY_VERTEX,
		D3D12_SHADER_VISIBILITY_HULL,
		D3D12_SHADER_VISIBILITY_HULL,
		D3D12_SHADER_VISIBILITY_DOMAIN,
		D3D12_SHADER_VISIBILITY_DOMAIN,
		D3D12_SHADER_VISIBILITY_GEOMETRY,
		D3D12_SHADER_VISIBILITY_GEOMETRY,
		D3D12_SHADER_VISIBILITY_PIXEL,
		D3D12_SHADER_VISIBILITY_PIXEL,
	};

	for (u32 i = 0; i < num_params; i += 2)
	{
		params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[i].ShaderVisibility = visibilities[i];
		params[i].DescriptorTable.NumDescriptorRanges = num_descriptor_ranges;
		params[i].DescriptorTable.pDescriptorRanges = descriptor_ranges;

		params[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[i + 1].ShaderVisibility = visibilities[i];
		params[i + 1].DescriptorTable.NumDescriptorRanges = 1;
		params[i + 1].DescriptorTable.pDescriptorRanges = &sampler_range;
	}

	D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {};
	root_sig_desc.NumStaticSamplers = 0;
	root_sig_desc.NumParameters = num_params;
	root_sig_desc.pParameters = params;
	root_sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ID3DBlob* root_sig_blob;
	ID3DBlob* root_sig_error;
		
	HRESULT hr = D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &root_sig_blob, &root_sig_error);
	ASSERT_HR(hr);
		
	hr = m_d3d_device->CreateRootSignature(0, root_sig_blob->GetBufferPointer(), root_sig_blob->GetBufferSize(), IID_PPV_ARGS(&m_graphics_root_sig));
	ASSERT_HR(hr);
}

void GpuDeviceDX12::createComputeRootSig()
{
	D3D12_DESCRIPTOR_RANGE sampler_range = {};
	sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
	sampler_range.BaseShaderRegister = 0;
	sampler_range.RegisterSpace = 0;
	sampler_range.NumDescriptors = GPU_SAMPLER_HEAP_COUNT;
	sampler_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	u32 const num_descriptor_ranges = 3;
	D3D12_DESCRIPTOR_RANGE descriptorRanges[num_descriptor_ranges];

	descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	descriptorRanges[0].BaseShaderRegister = 0;
	descriptorRanges[0].RegisterSpace = 0;
	descriptorRanges[0].NumDescriptors = GPU_RESOURCE_HEAP_CBV_COUNT;
	descriptorRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	descriptorRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRanges[1].BaseShaderRegister = 0;
	descriptorRanges[1].RegisterSpace = 0;
	descriptorRanges[1].NumDescriptors = GPU_RESOURCE_HEAP_SRV_COUNT;
	descriptorRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	descriptorRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	descriptorRanges[2].BaseShaderRegister = 0;
	descriptorRanges[2].RegisterSpace = 0;
	descriptorRanges[2].NumDescriptors = GPU_RESOURCE_HEAP_UAV_COUNT;
	descriptorRanges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	u32 const num_params = 2;
	D3D12_ROOT_PARAMETER params[num_params];
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[0].DescriptorTable.NumDescriptorRanges = num_descriptor_ranges;
	params[0].DescriptorTable.pDescriptorRanges = descriptorRanges;

	params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[1].DescriptorTable.NumDescriptorRanges = 1;
	params[1].DescriptorTable.pDescriptorRanges = &sampler_range;

	D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
	rootSigDesc.NumStaticSamplers = 0;
	rootSigDesc.NumParameters = num_params;
	rootSigDesc.pParameters = params;
	rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	ID3DBlob* rootSigBlob;
	ID3DBlob* rootSigError;
	
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &rootSigBlob, &rootSigError);
	ASSERT_HR(hr);

	hr = m_d3d_device->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(), IID_PPV_ARGS(&m_compute_root_sig));
	ASSERT_HR(hr);
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
		MAX_FRAME_COUNT,
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
	swapChainDesc.BufferCount = MAX_FRAME_COUNT;
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
		m_cmd_queue_mng.GetGraphicsQueue()->GetQueue(),
		m_window,
		&swapChainDesc,
		&fsSwapChainDesc,
		nullptr,
		swapChain.GetAddressOf()
	);

	ASSERT_HR(hr);

	hr = swapChain.As(&m_swap_chain);
	ASSERT_HR(hr);

	// This class does not support exclusive full-screen mode and prevents DXGI from responding to the ALT+ENTER shortcut
	hr = m_dxgi_factory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER);
	ASSERT_HR(hr);
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
	ASSERT_HR(hr);

	ComPtr<IDXGIOutput6> output6;
	hr = output.As(&output6);
	ASSERT_HR(hr);

	DXGI_OUTPUT_DESC1 desc = {};
	hr = output6->GetDesc1(&desc);
	ASSERT_HR(hr);

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
		ASSERT_HR(hr);
	}

}

void GpuDeviceDX12::updateSwapchain(u32 width, u32 height)
{
	ASSERT(m_window);

	// Release resources that are tied to the swap chain and update fence values.
	for (u32 n = 0; n < MAX_FRAME_COUNT; n++)
	{
		m_frame_resource[n].render_target.Reset();
	}

	const DXGI_FORMAT backBufferFormat = formatSrgbToLinear(m_backbuffer_format);

	// If the swap chain already exists, resize it, otherwise create one.
	if (m_swap_chain)
	{
		resizeSwapChain(width, height, backBufferFormat);
	}
	else
	{
		createSwapChain(width, height, backBufferFormat);
	}

	updateColorSpace();
}

void GpuDeviceDX12::createDepthBuffer(u32 width, u32 height)
{
	ASSERT(m_depthbuffer_format != DXGI_FORMAT_UNKNOWN);

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

	ASSERT_HR(hr);

	m_depth_stencil->SetName(L"Depth stencil");

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = m_depthbuffer_format;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	m_d3d_device->CreateDepthStencilView(m_depth_stencil.Get(), &dsvDesc, m_dsv_descriptor_heap->GetCPUDescriptorHandleForHeapStart());
}

void GpuDeviceDX12::UpdateViewportScissorRect(u32 width, u32 height)
{
	// Set the 3D rendering viewport and scissor rectangle to target the entire window.
	m_screen_viewport.TopLeftX = m_screen_viewport.TopLeftY = 0.f;
	m_screen_viewport.Width = static_cast<f32>(width);
	m_screen_viewport.Height = static_cast<f32>(height);
	m_screen_viewport.MinDepth = D3D12_MIN_DEPTH;
	m_screen_viewport.MaxDepth = D3D12_MAX_DEPTH;

	m_scissor_rect.left = m_scissor_rect.top = 0;
	m_scissor_rect.right = width;
	m_scissor_rect.bottom = height;
}

void GpuDeviceDX12::enableDebugLayer()
{
#ifdef _DEBUG
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
#endif // _DEBUG
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

#ifdef _DEBUG
	if (bEnableDebugLayer)
	{
		// Configure debug device (if active).
		ComPtr<ID3D12InfoQueue> d3dInfoQueue;
		if (SUCCEEDED(m_d3d_device.As(&d3dInfoQueue)))
		{
			d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
			d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
			d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

			D3D12_MESSAGE_SEVERITY Severities[] =
			{
				D3D12_MESSAGE_SEVERITY_INFO
			};

			// Suppress individual messages by their ID
			D3D12_MESSAGE_ID DenyIds[] =
			{
				D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,

				// This warning occurs when using capture frame while graphics debugging.
				D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,

				// This warning occurs when using capture frame while graphics debugging.
				D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,

				// This occurs when there are uninitialized descriptors in a descriptor table, even when a
				// shader does not access the missing descriptors.  I find this is common when switching
				// shader permutations and not wanting to change much code to reorder resources.
				D3D12_MESSAGE_ID_INVALID_DESCRIPTOR_HANDLE,

				// Triggered when a shader does not export all color components of a render target, such as
				// when only writing RGB to an R10G10B10A2 buffer, ignoring alpha.
				D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_PS_OUTPUT_RT_OUTPUT_MISMATCH,

				// This occurs when a descriptor table is unbound even when a shader does not access the missing
				// descriptors.  This is common with a root signature shared between disparate shaders that
				// don't all need the same types of resources.
				D3D12_MESSAGE_ID_COMMAND_LIST_DESCRIPTOR_TABLE_NOT_SET,

				// RESOURCE_BARRIER_DUPLICATE_SUBRESOURCE_TRANSITIONS
				(D3D12_MESSAGE_ID)1008,
			};

			D3D12_INFO_QUEUE_FILTER filter = {};
			filter.DenyList.NumSeverities = _countof(Severities);
			filter.DenyList.pSeverityList = Severities;
			filter.DenyList.NumIDs = _countof(DenyIds);
			filter.DenyList.pIDList = DenyIds;

			HRESULT hr = d3dInfoQueue->AddStorageFilterEntries(&filter);
			ASSERT_HR(hr);
		}
	}
#else
	UNUSED(bEnableDebugLayer);
#endif //_DEBUG
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

void GpuDeviceDX12::createDescriptorHeaps()
{
	// Render targets
	D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {};
	rtvDescriptorHeapDesc.NumDescriptors = MAX_FRAME_COUNT;
	rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

	HRESULT descrHeapCreated = m_d3d_device->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(m_rtv_descriptor_heap.ReleaseAndGetAddressOf()));
	ASSERT_HR(descrHeapCreated);

	m_rtv_descriptor_heap->SetName(L"DeviceResources");
	m_rtv_descriptor_size = m_d3d_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// Depth Stencil Views
	ASSERT(m_depthbuffer_format != DXGI_FORMAT_UNKNOWN);

	D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc = {};
	dsvDescriptorHeapDesc.NumDescriptors = 1;
	dsvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

	descrHeapCreated = m_d3d_device->CreateDescriptorHeap(&dsvDescriptorHeapDesc, IID_PPV_ARGS(&m_dsv_descriptor_heap));

	m_dsv_descriptor_heap->SetName(L"DeviceResources");
}

static ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, u32 frame_idx)
{
	ComPtr<ID3D12CommandAllocator> cmd_allocator = nullptr;

	HRESULT createdAllocator = device->CreateCommandAllocator(type, IID_PPV_ARGS(&cmd_allocator));
	ASSERT_HR(createdAllocator);

	wchar_t name[64] = {};
	swprintf_s(name, L"cmdallocator_frame_%u", frame_idx);
	cmd_allocator->SetName(name);

	return cmd_allocator;
}

ComPtr<ID3D12GraphicsCommandList> GpuDeviceDX12::CreateCommandList(D3D12_COMMAND_LIST_TYPE type, wchar_t* name)
{
	ComPtr<ID3D12GraphicsCommandList> cmd_list;

	HRESULT cmdListCreated = m_d3d_device->CreateCommandList(
		0,
		type,
		GetFrameResources()->cmd_allocator.GetAllocator(type),
		nullptr,
		IID_PPV_ARGS(&cmd_list));

	ASSERT_HR(cmdListCreated);

	HRESULT closed = cmd_list->Close(); 
	ASSERT_HR(closed);

	if (name)
	{
		cmd_list->SetName(name);
	}
	
	return cmd_list;
}

void GpuDeviceDX12::OpenCommandList(ID3D12GraphicsCommandList* cmd_list)
{
	FrameResource* frame_resource = GetFrameResources();
	CommandAllocator* cmd_allocator = &frame_resource->cmd_allocator;

	frame_resource->cmd_list_manager.OpenCommandList(cmd_list, cmd_allocator);
}

void GpuDeviceDX12::CloseAndEnqueueCommandList(ID3D12GraphicsCommandList* cmd_list)
{
	GetFrameResources()->CloseAndSubmitCommandList(cmd_list);
}

u64 GpuDeviceDX12::CloseAndSubmitCommandList(ID3D12GraphicsCommandList* cmd_list)
{
	GetFrameResources()->CloseCommandList(cmd_list);
	return m_cmd_queue_mng.ExecuteCommandList(cmd_list);
}

void GpuDeviceDX12::FlushQueuedGraphicsWork()
{
	return GetFrameResources()->SubmitQueuedWork(m_cmd_queue_mng.GetGraphicsQueue());
}

static ComPtr<ID3D12Resource> CreateRenderTarget(ID3D12Device* device, ID3D12DescriptorHeap* heap, IDXGISwapChain3* swap_chain, DXGI_FORMAT format, u32 descriptor_size, u32 frame_idx)
{
	ComPtr<ID3D12Resource> render_target;

	D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
	rtv_desc.Format = format;
	rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	swap_chain->GetBuffer(frame_idx, IID_PPV_ARGS(render_target.GetAddressOf()));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(heap->GetCPUDescriptorHandleForHeapStart(), frame_idx, descriptor_size);
	device->CreateRenderTargetView(render_target.Get(), &rtv_desc, rtv_handle);

	wchar_t name[64] = {};
	swprintf_s(name, L"render_target_frame_%u", frame_idx);
	render_target->SetName(name);

	return render_target;
}

void GpuDeviceDX12::createFrameResources()
{
	ID3D12Device* device = GetD3DDevice();

	wchar_t cmd_list_name[64];

	for (u32 i = 0; i < MAX_FRAME_COUNT; ++i)
	{
		swprintf_s(cmd_list_name, L"cmd_list_frame_%u", i);

		m_frame_resource[i].cmd_allocator.Create(device);
		m_frame_resource[i].command_list = CreateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_list_name);
		m_frame_resource[i].render_target = CreateRenderTarget(device, m_rtv_descriptor_heap.Get(), GetSwapChain(), m_backbuffer_format, m_rtv_descriptor_size, i);

		m_frame_resource[i].resource_descriptors_gpu.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024);
		m_frame_resource[i].sampler_descriptors_gpu.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 16);
		m_frame_resource[i].transient_resource_allocator.Create(device, 1024 * 1024 * 128);
	}
}

void GpuDeviceDX12::destroyFrameResource()
{
	for (u32 i = 0; i < MAX_FRAME_COUNT; ++i)
	{
		m_frame_resource[i].cmd_allocator.Destroy();
	}
}

GpuBuffer GpuDeviceDX12::CreateBuffer(GpuBufferDesc const& desc, void* initial_data, u32 initial_data_bytes)
{
	GpuBuffer buffer;
	buffer.desc = desc;

	u32 alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	if (desc.bind_flags & BindFlags::ConstantBuffer)
	{
		alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
	}
	
	u64 aligned_size = Memory::AlignValue(desc.sizes_bytes, alignment);

	D3D12_HEAP_PROPERTIES heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE;

	D3D12_RESOURCE_FLAGS resource_flags = D3D12_RESOURCE_FLAG_NONE;
	if (desc.bind_flags & BindFlags::UnorderedAccess)
	{
		resource_flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}

	D3D12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(aligned_size, resource_flags);
	D3D12_RESOURCE_STATES resource_states = D3D12_RESOURCE_STATE_COMMON;

	HRESULT created = m_d3d_device->CreateCommittedResource(
		&heap_props,
		heap_flags,
		&resource_desc,
		resource_states,
		nullptr,
		IID_PPV_ARGS(&buffer.resource)
	);
	ASSERT_HR(created);

	ID3D12GraphicsCommandList* cmdlist = GetCurrentCommandList();

	if (initial_data)
	{
		FrameResource* frame = GetFrameResources();
		ResourceAllocator& allocator = frame->transient_resource_allocator;
		
		u8* copy_src_buffer = allocator.Allocate(initial_data_bytes, alignment);
		memcpy(copy_src_buffer, initial_data, initial_data_bytes);
		
		cmdlist->CopyBufferRegion(
			buffer.resource.Get(), 
			0, 
			allocator.GetRootBuffer(), 
			allocator.CalculateOffset(copy_src_buffer), 
			initial_data_bytes);
	}

	if (desc.bind_flags & BindFlags::ConstantBuffer)
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
		cbv_desc.BufferLocation = buffer.resource->GetGPUVirtualAddress();
		cbv_desc.SizeInBytes = static_cast<u32>(aligned_size);

		buffer.cbv.ptr = m_resource_allocator.Allocate();
		m_d3d_device->CreateConstantBufferView(&cbv_desc, buffer.cbv);
	}

	if (desc.bind_flags & BindFlags::ShaderResource)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srv_desc.Buffer.FirstElement = 0;

		if (desc.misc_flags & ResourceFlags::AllowRawViews)
		{
			srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
			srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
			srv_desc.Buffer.NumElements = desc.sizes_bytes / 4;
		}
		else if (desc.misc_flags & ResourceFlags::StructuredBuffer)
		{
			srv_desc.Format = DXGI_FORMAT_UNKNOWN;
			srv_desc.Buffer.NumElements = desc.sizes_bytes / desc.stride_in_bytes;
			srv_desc.Buffer.StructureByteStride = desc.stride_in_bytes;
		}
		else // Typed buffer.
		{
			srv_desc.Format = desc.format;
			srv_desc.Buffer.NumElements = desc.sizes_bytes / desc.stride_in_bytes;
		}

		buffer.srv.ptr = m_resource_allocator.Allocate();
		m_d3d_device->CreateShaderResourceView(buffer.resource.Get(), &srv_desc, buffer.srv);
	}

	if (desc.bind_flags & BindFlags::UnorderedAccess)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
		uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uav_desc.Buffer.FirstElement = 0;

		if (desc.misc_flags & ResourceFlags::AllowRawViews)
		{
			uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
			uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
			uav_desc.Buffer.NumElements = desc.sizes_bytes / 4;
		}
		else if (desc.misc_flags & ResourceFlags::StructuredBuffer)
		{
			uav_desc.Format = DXGI_FORMAT_UNKNOWN;
			uav_desc.Buffer.NumElements = desc.sizes_bytes / desc.stride_in_bytes;
			uav_desc.Buffer.StructureByteStride = desc.stride_in_bytes;
		}
		else // Typed buffer.
		{
			uav_desc.Format = desc.format;
			uav_desc.Buffer.NumElements = desc.sizes_bytes / desc.stride_in_bytes;
		}

		buffer.uav.ptr = m_resource_allocator.Allocate();
		m_d3d_device->CreateUnorderedAccessView(buffer.resource.Get(), nullptr, &uav_desc, buffer.uav);
	}

	return buffer;
}
namespace Gfx
{
	static GpuDeviceDX12* g_gpu_device = nullptr;

	void CreateGpuDevice(void* main_window_handle, u32 flags)
	{
		ASSERT(g_gpu_device == nullptr);
		g_gpu_device = new GpuDeviceDX12();
		g_gpu_device->Init(main_window_handle, flags);
	}

	void DestroyGpuDevice()
	{
		ASSERT(g_gpu_device != nullptr);
		g_gpu_device->Flush();
		g_gpu_device->Destroy();
		delete g_gpu_device;
	}

	void BeginPresent()
	{
		g_gpu_device->BeginPresent();
	}
	
	void EndPresent()
	{
		g_gpu_device->EndPresent();
	}

	void TransitionBarrier(ID3D12Resource* resource, ID3D12GraphicsCommandList* cmd_list, ResourceState::Enum state_before, ResourceState::Enum state_after)
	{
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			resource,
			ResourceStateToDX12(state_before),
			ResourceStateToDX12(state_after)
		);

		cmd_list->ResourceBarrier(1, &barrier);
	}

	void TransitionBarriers(ID3D12Resource** resources, ID3D12GraphicsCommandList* cmd_list, u8 num_barriers, ResourceState::Enum state_before, ResourceState::Enum state_after)
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

		cmd_list->ResourceBarrier(num_barriers, barriers);
	}

	void WaitForFenceValue(ID3D12Fence* fence, uint64_t fenceValue, HANDLE fenceEvent, u32 durationMS)
	{
		// TODO(pgPW): Add hang detection here. Set timer, wait interval, if not completed, assert.

		if (fence->GetCompletedValue() < fenceValue)
		{
			HRESULT hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
			ASSERT_HR(hr);
			WaitForSingleObjectEx(fenceEvent, durationMS, FALSE);
		}
	}

	u64 Signal(ID3D12CommandQueue* commandQueue, ID3D12Fence* fence, u64 fenceValue)
	{
		HRESULT hr = commandQueue->Signal(fence, fenceValue);
		ASSERT_HR(hr);

		return fenceValue + 1;
	}

	void BindVertexBuffer(ID3D12GraphicsCommandList* command_list, GpuBuffer const * vertex_buffer, u8 slot, u32 offset)
	{
		D3D12_VERTEX_BUFFER_VIEW buffer_view;

		buffer_view.BufferLocation = vertex_buffer->resource->GetGPUVirtualAddress();
		buffer_view.SizeInBytes = vertex_buffer->desc.sizes_bytes;
		buffer_view.StrideInBytes = vertex_buffer->desc.stride_in_bytes;
		buffer_view.BufferLocation += static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(offset);
		buffer_view.SizeInBytes -= offset;

		command_list->IASetVertexBuffers(static_cast<u32>(slot), 1, &buffer_view);
	}

	void BindVertexBuffers(ID3D12GraphicsCommandList* command_list, GpuBuffer const ** vertex_buffers, u8 slot, u8 count, u32 const* offsets)
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

	ComPtr<ID3D12GraphicsCommandList> CreateCommandList(D3D12_COMMAND_LIST_TYPE type, wchar_t* name)
	{
		return g_gpu_device->CreateCommandList(type, name);
	}

	void OpenCommandList(ID3D12GraphicsCommandList* cmd_list)
	{
		g_gpu_device->OpenCommandList(cmd_list);
	}
	
	void EnqueueCommandList(ID3D12GraphicsCommandList* cmd_list)
	{
		g_gpu_device->CloseAndEnqueueCommandList(cmd_list);
	}

	void SubmitCommandList(ID3D12GraphicsCommandList* cmd_list)
	{
		g_gpu_device->CloseAndSubmitCommandList(cmd_list);
	}

	void FlushQueuedGraphicsWork()
	{
		g_gpu_device->FlushQueuedGraphicsWork();
	}

	GpuBuffer CreateVertexBuffer(GpuDeviceDX12* gpu_device, void* vertex_data, u32 vertex_bytes, u32 vertex_stride_bytes)
	{
		GpuBufferDesc desc;
		MemZeroSafe(desc);
		desc.usage = BufferUsage::Default;
		desc.sizes_bytes = vertex_bytes;
		desc.bind_flags = BindFlags::VertexBuffer;
		desc.stride_in_bytes = vertex_stride_bytes;

		GpuBuffer vertex_buffer = gpu_device->CreateBuffer(desc, vertex_data, vertex_bytes);

		TransitionBarrier(
			vertex_buffer.resource.Get(),
			gpu_device->GetCurrentCommandList(),
			ResourceState::Copy_Dest,
			ResourceState::Vertex_And_Constant_Buffer);

		return vertex_buffer;
	}

	GpuBuffer CreateIndexBuffer(GpuDeviceDX12* gpu_device, void* index_data, u32 index_bytes)
	{
		GpuBufferDesc desc;
		MemZeroSafe(desc);
		desc.usage = BufferUsage::Default;
		desc.sizes_bytes = index_bytes;
		desc.bind_flags = BindFlags::IndexBuffer;
		desc.format = DXGI_FORMAT_R16_UINT;
		desc.stride_in_bytes = sizeof(u16);

		GpuBuffer index_buffer = gpu_device->CreateBuffer(desc, index_data, index_bytes);

		TransitionBarrier(
			index_buffer.resource.Get(),
			gpu_device->GetCurrentCommandList(),
			ResourceState::Copy_Dest,
			ResourceState::Index_Buffer // ResourceState::Generic_Read?
		);

		return index_buffer;
	}

	Mesh CreateMesh(GpuDeviceDX12* gpu_device, void* vertex_data, u32 vertex_bytes, u32 vertex_stride_bytes, void* index_data, u32 index_bytes)
	{
		Mesh mesh;
		mesh.vertex_buffer_gpu = CreateVertexBuffer(gpu_device, vertex_data, vertex_bytes, vertex_stride_bytes);
		mesh.index_buffer_gpu = CreateIndexBuffer(gpu_device, index_data, index_bytes);

		return mesh;
	}

	void DrawMesh(Mesh const* mesh, ID3D12GraphicsCommandList* command_list)
	{
		BindVertexBuffer(command_list, &mesh->vertex_buffer_gpu, 0, 0);
		BindIndexBuffer(command_list, &mesh->index_buffer_gpu, 0);

		for (SubMesh const& submesh : mesh->submeshes)
		{
			command_list->DrawIndexedInstanced(submesh.num_indices, 1, submesh.first_index_location, submesh.base_vertex_location, 0);
		}
	}
}