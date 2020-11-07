#include "GpuDeviceDX12.h"
#include "Core.h"
#include "MemUtils.h"
#include "PSO.h"

#if defined(NTDDI_WIN10_RS2)
#define USES_DXGI6 1
#else
#define USES_DXGI6 0
#endif

#if USES_DXGI6
#include <dxgi1_6.h>
#else
#include <dxgi1_5.h>
#endif

#ifdef _DEBUG
#include <dxgidebug.h>
#endif

using Microsoft::WRL::ComPtr;

static constexpr size_t GPU_SAMPLER_HEAP_COUNT = 16;
static constexpr size_t GPU_RESOURCE_HEAP_CBV_COUNT = 12;
static constexpr size_t GPU_RESOURCE_HEAP_SRV_COUNT = 64;
static constexpr size_t GPU_RESOURCE_HEAP_UAV_COUNT = 8;
static constexpr size_t GPU_RESOURCE_HEAP_CBV_SRV_UAV_COUNT = GPU_RESOURCE_HEAP_CBV_COUNT + GPU_RESOURCE_HEAP_SRV_COUNT + GPU_RESOURCE_HEAP_UAV_COUNT;

static const u32 MAX_FRAME_COUNT = 2;

using namespace Gfx;

static D3D12_RESOURCE_STATES ResourceStateToDX12(ResourceState::Enum state)
{
	return static_cast<D3D12_RESOURCE_STATES>(state);
}

void TransitionBarrier(ID3D12Resource* resource, 
	ID3D12GraphicsCommandList* cmd_list, 
	ResourceState::Enum state_before, 
	ResourceState::Enum state_after, 
	u32 sub_resource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
	D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE)
{
	D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		resource,
		ResourceStateToDX12(state_before),
		ResourceStateToDX12(state_after),
		sub_resource,
		flags);

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

class CommandQueue
{
public:
	CommandQueue();

	void Create(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE cmd_type);
	void Release();

	bool IsFenceComplete(u64 fence_value);
	void InsertGpuWait(u64 fence_value);
	void InsertGpuWaitForOtherQueue(CommandQueue* other_queue, u64 fence_value);
	void InsertGpuWaitForOtherQueue(CommandQueue* other_queue);

	void WaitForFenceCpuBlocking(u64 fence_value);
	void WaitForQueueFinishedCpuBlocking() { WaitForFenceCpuBlocking(m_next_fence_value - 1); }

	ID3D12CommandQueue* GetQueue() { return m_cmd_queue.Get(); }

	u64 FetchCurrentFenceValue();
	u64 GetLastCompletedFence() const { return m_last_completed_fence_value; }
	u64 GetNextFenceValue() const { return m_next_fence_value; }
	ID3D12Fence* GetFence() { return m_fence.Get(); }

	u64 ExecuteCommandList(ID3D12CommandList* cmd_list);
	u64 ExecuteCommandLists(ID3D12CommandList** cmd_lists, u32 count);
	u64 Signal();

private:
#ifdef _DEBUG
	bool CommandQueue::IsFenceFromThisQueue(u64 fence);
#endif

	ComPtr<ID3D12CommandQueue> m_cmd_queue;
	D3D12_COMMAND_LIST_TYPE m_queue_type;

	std::mutex m_fence_mutex;
	std::mutex m_event_mutex;

	ComPtr<ID3D12Fence> m_fence;
	u64 m_next_fence_value;
	u64 m_last_completed_fence_value;
	HANDLE m_fence_event_handle;
};

class CommandQueueManager
{
public:
	void Create(ID3D12Device* device);
	void Release();

	CommandQueue* GetGraphicsQueue() { return &m_graphics; }
	CommandQueue* GetComputeQueue() { return &m_compute; }
	CommandQueue* GetCopyQueue() { return &m_copy; }

	CommandQueue* GetQueue(D3D12_COMMAND_LIST_TYPE type);

	u64 ExecuteCommandList(ID3D12CommandList* cmd_list);

	bool IsFenceComplete(u64 fence_value);
	void WaitForFenceCpuBlocking(u64 fence_value);
	void WaitForAllQueuesFinished();

private:
	CommandQueue m_graphics;
	CommandQueue m_compute;
	CommandQueue m_copy;
};

class DescriptorTableFrameAllocator
{
public:
	void Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 max_rename_count);
	void Destroy();

	void Reset(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE* null_descriptors_sampler_cbv_srv_uav);
	void BindDescriptor(Gfx::ShaderStage::Enum stage, u32 offset, D3D12_CPU_DESCRIPTOR_HANDLE const* descriptor, ID3D12Device* device);
	void Update(ID3D12Device* device, ID3D12GraphicsCommandList* command_list);

	ID3D12DescriptorHeap* GetGpuHeap() { return m_heap_gpu.Get(); }
	ID3D12DescriptorHeap* GetCpuHeap() { return m_heap_cpu.Get(); }

private:
	size_t GetBoundDescriptorHeapSize() const;

	ComPtr<ID3D12DescriptorHeap> m_heap_cpu = nullptr;
	ComPtr<ID3D12DescriptorHeap> m_heap_gpu = nullptr;
	D3D12_DESCRIPTOR_HEAP_TYPE m_descriptor_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
	u32 m_item_size = 0;
	u32 m_item_count = 0;
	u32 m_ring_offset = 0;
	bool m_is_dirty[Gfx::ShaderStage::Count];
	D3D12_CPU_DESCRIPTOR_HANDLE const** m_bound_descriptors = nullptr;
};

class UploadBufferAllocator
{
public:
	void Create(ID3D12Device* device, size_t size_bytes);
	void Clear();
	void Destroy();

	u8* Allocate(size_t size_bytes, size_t alignment);
	u64 CalculateOffset(u8* address);

	inline ID3D12Resource* GetRootBuffer() { return m_resource.Get(); }

private:
	ComPtr<ID3D12Resource> m_resource = nullptr;
	std::mutex m_allocation_lock;
	u8* m_data_begin = 0;
	u8* m_data_current = 0;
	u8* m_data_end = 0;
};

class DescriptorAllocator
{
public:
	void Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 max_count);
	u64 Allocate();

private:
	ComPtr<ID3D12DescriptorHeap> m_heap = nullptr;
	std::atomic<u32> m_item_count = 0;
	D3D12_DESCRIPTOR_HEAP_TYPE m_type;
	u32 m_max_count = 0;
	u32 m_item_size = 0;
};

class CommandAllocator
{
public:
	void Create(ID3D12Device* device);
	void Destroy();
	void Reset();

	ID3D12CommandAllocator* GetAllocator(D3D12_COMMAND_LIST_TYPE type);
private:
	ID3D12CommandAllocator** GetAllocatorSet(D3D12_COMMAND_LIST_TYPE type);

	ID3D12CommandAllocator** m_gfx_allocators = nullptr;
	ID3D12CommandAllocator** m_copy_allocators = nullptr;
	ID3D12CommandAllocator** m_compute_allocators = nullptr;
};

class CommandListManager
{
public:
	CommandListManager();

	ID3D12GraphicsCommandList** GetListSetByType(D3D12_COMMAND_LIST_TYPE type);
	ID3D12GraphicsCommandList** GetCommandListSlot(s32 thread_id, D3D12_COMMAND_LIST_TYPE type);

	void OpenCommandList(ID3D12GraphicsCommandList* cmdlist, ID3D12CommandAllocator* cmd_allocator);
	void CloseCommandList(ID3D12GraphicsCommandList* cmdlist);
	bool AreAllCommandListsClosed();
	bool IsCommandListOpenOnThisThread(D3D12_COMMAND_LIST_TYPE type);
	bool IsCommandListOpenOnThisThread(ID3D12GraphicsCommandList* cmdlist);

private:
	ID3D12GraphicsCommandList* m_active_gfx_lists[NUM_MAX_THREADS];
	ID3D12GraphicsCommandList* m_active_copy_lists[NUM_MAX_THREADS];
	ID3D12GraphicsCommandList* m_active_compute_lists[NUM_MAX_THREADS];
};

// TODO: Expose this so we can use it for other "context-like" situations.
class FullFrameCommandList
{
public:
	void Create(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, wchar_t const* name)
	{
		HRESULT hr = device->CreateCommandAllocator(type, IID_PPV_ARGS(m_allocator.ReleaseAndGetAddressOf()));
		ASSERT_HR(hr);

		if (name) // TODO: wchar version of miniprintf
		{
			wchar_t full_name[128];
			swprintf_s(full_name, L"%s_alloc", name);
			m_allocator->SetName(name);
		}

		hr = device->CreateCommandList(
			0,
			type,
			m_allocator.Get(),
			nullptr,
			IID_PPV_ARGS(&m_commandlist));

		ASSERT_HR(hr);
		VERIFY_HR(m_commandlist->Close());

		if (name)
		{
			wchar_t full_name[128];
			swprintf_s(full_name, L"%s_list", name);
			m_commandlist->SetName(name);
		}
	}

	void Open()
	{
		VERIFY_HR(m_commandlist->Reset(m_allocator.Get(), nullptr));
	}

	void Close()
	{
		VERIFY_HR(m_commandlist->Close());
	}

	ID3D12GraphicsCommandList* GetCommandlist()
	{
		return m_commandlist.Get();
	}

private:
	ComPtr<ID3D12GraphicsCommandList> m_commandlist;
	ComPtr<ID3D12CommandAllocator> m_allocator;
};

class FrameResource
{
public:
	u64 end_of_frame_fence;

	DescriptorTableFrameAllocator resource_descriptors_gpu;
	DescriptorTableFrameAllocator sampler_descriptors_gpu;

	UploadBufferAllocator transient_upload_buffers;

	FullFrameCommandList present_cmdlist;

	ID3D12Resource* GetRenderTarget()
	{
		return m_render_target.Get();
	}

	void Create(ID3D12Device* device, u32 frame_index, ComPtr<ID3D12Resource> render_target)
	{
		m_cmd_allocator.Create(device);

		wchar_t cmd_list_name[64];
		swprintf_s(cmd_list_name, L"cmd_list_frame_%u", frame_index);
		command_list = CreateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_list_name);

		present_cmdlist.Create(device, D3D12_COMMAND_LIST_TYPE_DIRECT, L"present_cmds");

		m_render_target = render_target;

		resource_descriptors_gpu.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024);
		sampler_descriptors_gpu.Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 16);
		transient_upload_buffers.Create(device, 1024 * 1024 * 128);
	}

	void Destroy()
	{
		ASSERT(m_cmd_list_manager.AreAllCommandListsClosed());
		m_cmd_allocator.Destroy();
		transient_upload_buffers.Destroy();
	}

	bool AreAllCommandListsClosed()
	{
		return m_cmd_list_manager.AreAllCommandListsClosed();
	}

	void ResetCommandAllocators()
	{
		ASSERT(m_cmd_list_manager.AreAllCommandListsClosed());
		m_cmd_allocator.Reset();
	}

	FullFrameCommandList* GetPresentCommandlist()
	{
		return &present_cmdlist;
	}

	void OpenCommandList(ID3D12GraphicsCommandList* cmd_list)
	{
		D3D12_COMMAND_LIST_TYPE type = cmd_list->GetType();
		ASSERT(!m_cmd_list_manager.IsCommandListOpenOnThisThread(type));

		ID3D12CommandAllocator* allocator = GetCmdAllocatorForThread(type);

		m_cmd_list_manager.OpenCommandList(cmd_list, allocator);
	}

	void CloseCommandList(ID3D12GraphicsCommandList* cmd_list)
	{
		m_cmd_list_manager.CloseCommandList(cmd_list);
	}

	inline ID3D12CommandAllocator* GetCmdAllocatorForThread(D3D12_COMMAND_LIST_TYPE type)
	{
		// Ensure no CommandList of this type is already open, as it would be using this
		// allocator. Might be a bit restrictive, tbd.
		ASSERT(!m_cmd_list_manager.IsCommandListOpenOnThisThread(type));
		return m_cmd_allocator.GetAllocator(type);
	}

	//inline Gfx::Commandlist GetCommandlist() { return command_list; }

private:
	ComPtr<ID3D12Resource> m_render_target;
	CommandAllocator m_cmd_allocator;
	CommandListManager m_cmd_list_manager;
	Gfx::Commandlist command_list;

	std::mutex submit_lock;
};

class GpuDeviceDX12
{
public:
	void Init(void* windowHandle, u32 output_width, u32 output_height, u32 initFlags);
	void Destroy();
	bool IsTearingAllowed();

	void BeginPresent();
	void EndPresent();
	void Flush();

	void UpdateConstantBindings(ID3D12GraphicsCommandList* cmd_list);

	Gfx::GpuBuffer CreateBuffer(ID3D12GraphicsCommandList* cmd_list, GpuBufferDesc const& desc, void* initial_data, wchar_t* name = nullptr);
	void UpdateBuffer(ID3D12GraphicsCommandList* cmd_list, GpuBuffer const* buffer, void* data, u32 size_bytes);

	inline ID3D12Device* GetD3DDevice() const { return m_d3d_device.Get(); }
	
	__forceinline ID3D12GraphicsCommandList* HandleToCommandList(Gfx::Commandlist handle)
	{
		ASSERT(handle.IsValid());
		return m_external_cmd_lists[handle.handle];
	}

	Gfx::Commandlist CreateCommandList(D3D12_COMMAND_LIST_TYPE type, wchar_t* name = nullptr);

	ComPtr<ID3D12PipelineState> CreateGraphicsPSO(D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc);

	void OpenCommandList(Gfx::Commandlist handle);
	u64 CloseAndSubmitCommandList(Gfx::Commandlist handle);
	void WaitForFenceValueCpuBlocking(u64 fenceValue);

	void SetupGfxCommandList(ID3D12GraphicsCommandList* cmdlist);

	DXGI_FORMAT GetBackBufferFormat() const { return m_backbuffer_format; }
	DXGI_FORMAT GetDSFormat() const { return m_depthbuffer_format; }

	void BindConstantBuffer(GpuBuffer const* constant_buffer, ShaderStage::Enum stage, u8 slot);

private:
	void enableDebugLayer();
	bool checkTearingSupport();
	void createDevice(bool bEnableDebugLayer);
	void checkFeatureLevel();
	void createDescriptorHeaps();
	void createGraphicsRootSig();
	void createComputeRootSig();
	void createNullResources();

	void updateSwapchain(u32 width, u32 height);
	void resizeSwapChain(u32 width, u32 height, DXGI_FORMAT format);
	void createSwapChain(u32 width, u32 height, DXGI_FORMAT format);
	void updateColorSpace();
	void createDepthBuffer(u32 width, u32 height);
	void UpdateViewportScissorRect(u32 width, u32 height);

	void MoveToNextFrame();

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView() const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const;

	inline ComPtr<IDXGISwapChain3>	  GetSwapChain() const { return m_swap_chain; }
	inline D3D12_VIEWPORT             GetScreenViewport() const { return m_screen_viewport; }
	inline D3D12_RECT                 GetScissorRect() const { return m_scissor_rect; }
	inline u32	                      GetCurrentFrameIndex() const { return m_frame_index; }

	u32									m_frame_index;
	u32									m_init_flags;
	u32									m_backbuffer_width;
	u32									m_backbuffer_height;

	u64									m_frame_setup_fence;

	ComPtr<ID3D12Device>                m_d3d_device;
	CommandQueueManager					m_cmd_queue_mng;

	// Swap chain objects.
	ComPtr<IDXGIFactory4>               m_dxgi_factory;
	ComPtr<IDXGISwapChain3>             m_swap_chain;
	ComPtr<ID3D12Resource>              m_depth_stencil;

	// Direct3D rendering objects.
	ComPtr<ID3D12DescriptorHeap>        m_rtv_descriptor_heap;
	ComPtr<ID3D12DescriptorHeap>        m_dsv_descriptor_heap;
	u32									m_rtv_descriptor_size;
	D3D12_VIEWPORT                      m_screen_viewport;
	D3D12_RECT                          m_scissor_rect;

	// Direct3D properties.
	DXGI_FORMAT                         m_backbuffer_format;
	DXGI_FORMAT                         m_depthbuffer_format;
	D3D_FEATURE_LEVEL                   m_d3d_min_feature_level;
	DXGI_COLOR_SPACE_TYPE               m_color_space;

	// Cached device properties.
	HWND                                m_window;
	D3D_FEATURE_LEVEL                   m_d3d_feature_level;
	DWORD                               m_dxgi_factory_flags;
	RECT                                m_output_size;

	ComPtr<ID3D12RootSignature>			m_graphics_root_sig;
	ComPtr<ID3D12RootSignature>			m_compute_root_sig;

	D3D12_CPU_DESCRIPTOR_HANDLE			m_null_sampler;
	D3D12_CPU_DESCRIPTOR_HANDLE			m_null_cbv;
	D3D12_CPU_DESCRIPTOR_HANDLE			m_null_srv;
	D3D12_CPU_DESCRIPTOR_HANDLE			m_null_uav;

	FrameResource m_frame_resource[MAX_FRAME_COUNT];
	inline FrameResource* GetFrameResources() { return &m_frame_resource[m_frame_index]; }
	void createFrameResources();
	void destroyFrameResource();

	DescriptorAllocator m_rt_alloctor;
	DescriptorAllocator m_ds_allocator;
	DescriptorAllocator m_resource_allocator;
	DescriptorAllocator m_sampler_allocator;

	enum { MAX_CMD_LISTS = 30 };
	Array<ID3D12GraphicsCommandList*, MAX_CMD_LISTS, AtomicCounterPolicy> m_external_cmd_lists;
	CommandAllocator m_cmdlist_creation_allocator;

	enum { MAX_BUFFERS = 256 };
	Array<ID3D12Resource*, MAX_BUFFERS, AtomicCounterPolicy> m_created_buffers;

	//ComPtr<ID3D12GraphicsCommandList> CreateCommandListInternal(D3D12_COMMAND_LIST_TYPE type, wchar_t* name);

	//ResourceAllocator m_buffer_upload_allocator;
	//ResourceAllocator m_texture_upload_allocator;
};


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
	dst_staging.ptr += index * m_item_size;
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

void UploadBufferAllocator::Create(ID3D12Device* device, size_t size_bytes)
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

void UploadBufferAllocator::Destroy()
{
	m_resource->Unmap(0, nullptr);
}

u8* UploadBufferAllocator::Allocate(size_t size_bytes, size_t alignment)
{
	ScopedLock lock(m_allocation_lock);

	ptrdiff_t alignment_padding = Memory::GetAlignmentAdjustment(m_data_current, alignment);
	ASSERT((m_data_current + size_bytes + alignment_padding) <= m_data_end);

	u8* allocation = Memory::AlignAddress(m_data_current, alignment);
	m_data_current += (size_bytes + alignment_padding);

	return allocation;
}

u64 UploadBufferAllocator::CalculateOffset(u8* address)
{
	ASSERT(address >= m_data_begin && address < m_data_end);
	return static_cast<u64>(address - m_data_begin);
}

void UploadBufferAllocator::Clear()
{
	ScopedLock lock(m_allocation_lock);

	m_data_current = m_data_begin;
}

void DescriptorAllocator::Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 max_count)
{
	D3D12_DESCRIPTOR_HEAP_DESC desc;
	desc.NodeMask = 0;
	desc.NumDescriptors = max_count;
	desc.Type = type;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_heap.ReleaseAndGetAddressOf()));
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

namespace Gfx
{
	void RegisterCommandProducerThread()
	{
		ASSERT(g_cmd_threads_registered.load() < NUM_MAX_THREADS);
		tls_cmd_allocator_idx = g_cmd_threads_registered++;
	}
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
	for (u32 thread_id = 0; thread_id < NUM_MAX_THREADS; ++thread_id)
	{
		m_gfx_allocators[thread_id]->Release();
		m_copy_allocators[thread_id]->Release();
		m_compute_allocators[thread_id]->Release();
	}

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
		return m_active_gfx_lists;
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

void CommandListManager::OpenCommandList(ID3D12GraphicsCommandList* cmdlist, ID3D12CommandAllocator* cmd_allocator)
{
	s32 thread_id = GetCmdProducerThreadId();
	D3D12_COMMAND_LIST_TYPE list_type = cmdlist->GetType();

	ID3D12GraphicsCommandList** list_slot = GetCommandListSlot(thread_id, list_type);
	ASSERT_F(!IsCommandListOpenOnThisThread(list_type), "Another command list is already open on this thread!");

	VERIFY_HR(cmdlist->Reset(cmd_allocator, nullptr));
	*list_slot = cmdlist;
}

void CommandListManager::CloseCommandList(ID3D12GraphicsCommandList* cmdlist)
{
	s32 thread_id = GetCmdProducerThreadId();
	D3D12_COMMAND_LIST_TYPE list_type = cmdlist->GetType();

	ASSERT_F(IsCommandListOpenOnThisThread(cmdlist), "Attempting to close a command list that is not open on this thread!");
	VERIFY_HR(cmdlist->Close());

	ID3D12GraphicsCommandList** list_slot = GetCommandListSlot(thread_id, list_type);
	*list_slot = nullptr;
}

bool CommandListManager::AreAllCommandListsClosed()
{
	for (u32 i = 0; i < NUM_MAX_THREADS; ++i)
	{
		if (m_active_gfx_lists[i] != nullptr) return false;
		if (m_active_copy_lists[i] != nullptr) return false;
		if (m_active_compute_lists[i] != nullptr) return false;
	}

	return true;
}

bool CommandListManager::IsCommandListOpenOnThisThread(D3D12_COMMAND_LIST_TYPE type)
{
	s32 thread_id = GetCmdProducerThreadId();
	ID3D12GraphicsCommandList** list_slot = GetCommandListSlot(thread_id, type);
	
	return (*list_slot != nullptr);
}

bool CommandListManager::IsCommandListOpenOnThisThread(ID3D12GraphicsCommandList* cmdlist)
{
	s32 thread_id = GetCmdProducerThreadId();
	ID3D12GraphicsCommandList** list_slot = GetCommandListSlot(thread_id, cmdlist->GetType());

	return (*list_slot == cmdlist);
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
	HRESULT hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(m_cmd_queue.ReleaseAndGetAddressOf()));
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
	return Signal();
}

u64 CommandQueue::ExecuteCommandLists(ID3D12CommandList** cmd_lists, u32 count)
{
	m_cmd_queue->ExecuteCommandLists(count, cmd_lists);
	return Signal();
}

u64 CommandQueue::Signal()
{
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
	frame->ResetCommandAllocators();

	FullFrameCommandList* present_cmds = frame->GetPresentCommandlist();
	present_cmds->Open();

	ID3D12GraphicsCommandList* cmdlist = present_cmds->GetCommandlist();

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
	frame->transient_upload_buffers.Clear();

	// Transition the render target into the correct state to allow for drawing into it.
	TransitionBarrier(frame->GetRenderTarget(), cmdlist, ResourceState::Present, ResourceState::Render_Target);

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

	// Send the command list off to the GPU for processing.
	present_cmds->Close();
	m_frame_setup_fence = m_cmd_queue_mng.ExecuteCommandList(cmdlist);
	m_cmd_queue_mng.WaitForFenceCpuBlocking(m_frame_setup_fence);
}

void GpuDeviceDX12::EndPresent()
{
	ComPtr<IDXGISwapChain3> swapchain = GetSwapChain();

	FrameResource* frame = GetFrameResources();
	FullFrameCommandList* present_cmds = frame->GetPresentCommandlist();
	ID3D12GraphicsCommandList* cmdlist = present_cmds->GetCommandlist();

	present_cmds->Open();
	SetupGfxCommandList(cmdlist);

	//frame->resource_descriptors_gpu.Update(device, cmdlist);
	//frame->sampler_descriptors_gpu.Update(device, cmdlist);

	// Transition the render target to the state that allows it to be presented to the display.
	TransitionBarrier(frame->GetRenderTarget(), cmdlist, ResourceState::Render_Target, ResourceState::Present);

	// Send the command list off to the GPU for processing.
	present_cmds->Close();
	m_cmd_queue_mng.ExecuteCommandList(cmdlist);

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

	frame->end_of_frame_fence = m_cmd_queue_mng.GetGraphicsQueue()->Signal();

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

	MoveToNextFrame(); // TODO(): shouldnt we be doing this waiting for last frame (not this new one)?
}

void GpuDeviceDX12::Flush()
{
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
ComPtr<IDXGIAdapter1> getFirstAvailableHardwareAdapter(ComPtr<IDXGIFactory4> dxgiFactory, D3D_FEATURE_LEVEL minFeatureLevel)
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
	return adapter;
}

void GpuDeviceDX12::Init(void* windowHandle, u32 output_width, u32 output_height, u32 initFlags)
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
	m_output_size = { 0, 0, (long)output_width, (long)output_height};
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

	m_cmdlist_creation_allocator.Create(GetD3DDevice());

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

	//m_buffer_upload_allocator.Create(device, 256 * 1024 * 1024); // TODO: reset
	//m_texture_upload_allocator.Create(device, 256 * 1024 * 1024); // TODO: reset

	createNullResources();
}

void GpuDeviceDX12::Destroy()
{
	for (ID3D12GraphicsCommandList* cmd_list : m_external_cmd_lists)
	{
		cmd_list->Release();
	}

	for (ID3D12Resource* buffer : m_created_buffers)
	{
		buffer->Release();
	}
	
	destroyFrameResource();

	m_cmdlist_creation_allocator.Destroy();

	m_d3d_device = nullptr;
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
		swapChain.ReleaseAndGetAddressOf()
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
	/*for (u32 n = 0; n < MAX_FRAME_COUNT; n++)
	{
		m_frame_resource[n].render_target.Reset();
	}*/

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
	ComPtr<IDXGIAdapter1> adapter = getFirstAvailableHardwareAdapter(m_dxgi_factory, m_d3d_min_feature_level);

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

	m_rtv_descriptor_heap->SetName(L"RtvHeap");
	m_rtv_descriptor_size = m_d3d_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// Depth Stencil Views
	ASSERT(m_depthbuffer_format != DXGI_FORMAT_UNKNOWN);

	D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc = {};
	dsvDescriptorHeapDesc.NumDescriptors = 1;
	dsvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

	descrHeapCreated = m_d3d_device->CreateDescriptorHeap(&dsvDescriptorHeapDesc, IID_PPV_ARGS(m_dsv_descriptor_heap.ReleaseAndGetAddressOf()));

	m_dsv_descriptor_heap->SetName(L"DsvHeap");
}

static ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, u32 frame_idx)
{
	ComPtr<ID3D12CommandAllocator> cmd_allocator = nullptr;

	HRESULT createdAllocator = device->CreateCommandAllocator(type, IID_PPV_ARGS(cmd_allocator.ReleaseAndGetAddressOf()));
	ASSERT_HR(createdAllocator);

	wchar_t name[64] = {};
	swprintf_s(name, L"cmdallocator_frame_%u", frame_idx);
	cmd_allocator->SetName(name);

	return cmd_allocator;
}

Gfx::Commandlist GpuDeviceDX12::CreateCommandList(D3D12_COMMAND_LIST_TYPE type, wchar_t* name)
{
	ID3D12GraphicsCommandList* cmd_list = nullptr;

	HRESULT cmdListCreated = m_d3d_device->CreateCommandList(
		0,
		type,
		m_cmdlist_creation_allocator.GetAllocator(type),
		nullptr,
		IID_PPV_ARGS(&cmd_list));

	ASSERT_HR(cmdListCreated);
	VERIFY_HR(cmd_list->Close());

	if (name)
	{
		cmd_list->SetName(name);
	}

	ID3D12GraphicsCommandList** next_slot = m_external_cmd_lists.PushBack();
	*next_slot = cmd_list;

	return Gfx::Commandlist{ (s32)m_external_cmd_lists.IndexOf(next_slot) };
}

ComPtr<ID3D12PipelineState> GpuDeviceDX12::CreateGraphicsPSO(D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc)
{
	ComPtr<ID3D12PipelineState> pso;

	desc->pRootSignature = m_graphics_root_sig.Get();
	HRESULT result = m_d3d_device->CreateGraphicsPipelineState(desc, IID_PPV_ARGS(&pso));
	if (SUCCEEDED(result))
	{
		return pso;
	}
	else
	{
		return nullptr;
	}
}

void GpuDeviceDX12::OpenCommandList(Gfx::Commandlist handle)
{
	ID3D12GraphicsCommandList* cmdlist = HandleToCommandList(handle);
	FrameResource* frame = GetFrameResources();
	frame->OpenCommandList(cmdlist);

	SetupGfxCommandList(cmdlist);
}

void GpuDeviceDX12::SetupGfxCommandList(ID3D12GraphicsCommandList* cmdlist)
{
	FrameResource* frame = GetFrameResources();

	ID3D12DescriptorHeap* heaps[] =
	{
		frame->resource_descriptors_gpu.GetGpuHeap(),
		frame->sampler_descriptors_gpu.GetGpuHeap()
	};

	cmdlist->SetDescriptorHeaps(ARRAY_SIZE(heaps), heaps);
	cmdlist->SetGraphicsRootSignature(m_graphics_root_sig.Get());
	cmdlist->SetComputeRootSignature(m_compute_root_sig.Get());

	D3D12_VIEWPORT viewport = GetScreenViewport();
	D3D12_RECT scissorRect = GetScissorRect();
	cmdlist->RSSetViewports(1, &viewport);
	cmdlist->RSSetScissorRects(1, &scissorRect);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptor = GetRenderTargetView();
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvDescriptor = GetDepthStencilView();

	cmdlist->OMSetRenderTargets(1, &rtvDescriptor, FALSE, &dsvDescriptor);
}

u64 GpuDeviceDX12::CloseAndSubmitCommandList(Gfx::Commandlist handle)
{
	ID3D12GraphicsCommandList* cmd_list = HandleToCommandList(handle);

	GetFrameResources()->CloseCommandList(cmd_list);
	return m_cmd_queue_mng.ExecuteCommandList(cmd_list);
}

//u64 GpuDeviceDX12::SubmitCommandLists()
//{
//	CommandQueue* gfx_queue = m_cmd_queue_mng.GetGraphicsQueue();
//	return gfx_queue->ExecuteCommandLists((ID3D12CommandList**)work.Data(), work.Size());
//}

void GpuDeviceDX12::WaitForFenceValueCpuBlocking(u64 fence_value)
{
	m_cmd_queue_mng.WaitForFenceCpuBlocking(fence_value);
}

void GpuDeviceDX12::BindConstantBuffer(GpuBuffer const* constant_buffer, ShaderStage::Enum stage, u8 slot)
{
	FrameResource* frameResource = GetFrameResources();
	frameResource->resource_descriptors_gpu.BindDescriptor(stage, slot, &constant_buffer->cbv, m_d3d_device.Get());
}

static ComPtr<ID3D12Resource> CreateRenderTarget(ID3D12Device* device, ID3D12DescriptorHeap* heap, ComPtr<IDXGISwapChain3> swap_chain, DXGI_FORMAT format, u32 descriptor_size, u32 frame_idx)
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

	for (u32 i = 0; i < MAX_FRAME_COUNT; ++i)
	{
		m_frame_resource[i].Create(device, i, CreateRenderTarget(device, m_rtv_descriptor_heap.Get(), GetSwapChain(), m_backbuffer_format, m_rtv_descriptor_size, i));
	}
}

void GpuDeviceDX12::destroyFrameResource()
{
	for (u32 i = 0; i < MAX_FRAME_COUNT; ++i)
	{
		m_frame_resource[i].Destroy();
	}
}

void GpuDeviceDX12::UpdateConstantBindings(ID3D12GraphicsCommandList* cmd_list)
{
	FrameResource* frame = GetFrameResources();
	frame->resource_descriptors_gpu.Update(GetD3DDevice(), cmd_list);
	frame->sampler_descriptors_gpu.Update(GetD3DDevice(), cmd_list);
}

GpuBuffer GpuDeviceDX12::CreateBuffer(ID3D12GraphicsCommandList* cmd_list, GpuBufferDesc const& desc, void* initial_data, wchar_t* name)
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

	ID3D12Resource** resourceSlot = m_created_buffers.PushBack();

	HRESULT created = m_d3d_device->CreateCommittedResource(
		&heap_props,
		heap_flags,
		&resource_desc,
		resource_states,
		nullptr,
		IID_PPV_ARGS(resourceSlot)
	);
	ASSERT_HR(created);

	buffer.resource = *resourceSlot;

	if (name)
	{
		buffer.resource->SetName(name);
	}

	if (initial_data)
	{
		FrameResource* frame = GetFrameResources();
		UploadBufferAllocator& allocator = frame->transient_upload_buffers;
		
		u8* copy_src_buffer = allocator.Allocate(desc.sizes_bytes, alignment);
		memcpy(copy_src_buffer, initial_data, desc.sizes_bytes);
		
		cmd_list->CopyBufferRegion(
			buffer.resource, 
			0, 
			allocator.GetRootBuffer(), 
			allocator.CalculateOffset(copy_src_buffer), 
			desc.sizes_bytes);
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
		m_d3d_device->CreateShaderResourceView(buffer.resource, &srv_desc, buffer.srv);
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
		m_d3d_device->CreateUnorderedAccessView(buffer.resource, nullptr, &uav_desc, buffer.uav);
	}

	return buffer;
}

void GpuDeviceDX12::UpdateBuffer(ID3D12GraphicsCommandList* cmd_list, GpuBuffer const* buffer, void* data, u32 size_bytes)
{
	u64 alignment = 0;
	if (buffer->desc.bind_flags & BindFlags::ConstantBuffer)
	{
		alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
	}
	else
	{
		alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	}

	ResourceState::Enum state_before = ResourceState::Common;
	ResourceState::Enum state_after = ResourceState::Copy_Dest;

	if (buffer->desc.bind_flags & BindFlags::ConstantBuffer || buffer->desc.bind_flags & BindFlags::VertexBuffer)
	{
		state_before = ResourceState::Vertex_And_Constant_Buffer;
	}
	else if (buffer->desc.bind_flags & BindFlags::IndexBuffer)
	{
		state_before = ResourceState::Index_Buffer;
	}

	TransitionBarrier(buffer->resource, cmd_list, state_before, state_after);

	FrameResource* frame = GetFrameResources();
	u8* upload_memory = frame->transient_upload_buffers.Allocate(size_bytes, alignment);
	memcpy(upload_memory, data, size_bytes);
	cmd_list->CopyBufferRegion(
		buffer->resource,
		0,
		frame->transient_upload_buffers.GetRootBuffer(),
		frame->transient_upload_buffers.CalculateOffset(upload_memory),
		size_bytes
	);

	TransitionBarrier(buffer->resource, cmd_list, state_after, state_before);
}

namespace Gfx
{
	static GpuDeviceDX12* g_gpu_device = nullptr;
	static PSOCache* g_pso_cache = nullptr;

	void CreateGpuDevice(void* main_window_handle, u32 output_width, u32 output_height, u32 flags)
	{
		ASSERT(g_gpu_device == nullptr);
		g_gpu_device = new GpuDeviceDX12();
		g_gpu_device->Init(main_window_handle, output_width, output_height, flags);
		g_pso_cache = new PSOCache();
	}

	void DestroyGpuDevice()
	{
		ASSERT(g_gpu_device != nullptr);

		g_pso_cache->Destroy();
		delete g_pso_cache;

		g_gpu_device->Flush();
		g_gpu_device->Destroy();

		delete g_gpu_device;
		
#ifdef _DEBUG
#if 0 // Enable to actually be able to debug resource leaks, since the device ref held by IDXGIDebug1 causes a throw...
		IDXGIInfoQueue* dxgiInfoQueue;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiInfoQueue))))
		{
			dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING, false);
			dxgiInfoQueue->SetBreakOnCategory(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_CATEGORY_STATE_CREATION, false);
			dxgiInfoQueue->Release();
		}
#endif

		IDXGIDebug1* pDebug = nullptr;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
		{
			pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
			pDebug->Release();
		}
#endif
	}

	void BeginPresent()
	{
		g_gpu_device->BeginPresent();
	}

	void EndPresent()
	{
		g_gpu_device->EndPresent();
	}

	DXGI_FORMAT GetBackBufferFormat()
	{
		return g_gpu_device->GetBackBufferFormat();
	}

	DXGI_FORMAT GetDSFormat()
	{
		return g_gpu_device->GetDSFormat();
	}

	void TransitionBarrier(ID3D12Resource* resource, Commandlist cmd_list_handle, ResourceState::Enum state_before, ResourceState::Enum state_after)
	{
		ID3D12GraphicsCommandList* cmd_list = g_gpu_device->HandleToCommandList(cmd_list_handle);
		TransitionBarrier(resource, cmd_list, state_before, state_after);
	}

	void TransitionBarriers(ID3D12Resource** resources, Commandlist cmd_list_handle, u8 num_barriers, ResourceState::Enum state_before, ResourceState::Enum state_after)
	{
		ID3D12GraphicsCommandList* cmd_list = g_gpu_device->HandleToCommandList(cmd_list_handle);
		TransitionBarriers(resources, cmd_list, num_barriers, state_before, state_after);
	}

	void WaitForFenceValueCpuBlocking(u64 fenceValue)
	{
		g_gpu_device->WaitForFenceValueCpuBlocking(fenceValue);
	}

	Commandlist CreateCommandList(D3D12_COMMAND_LIST_TYPE type, wchar_t* name)
	{
		return g_gpu_device->CreateCommandList(type, name);
	}

	void OpenCommandList(Commandlist cmd_list)
	{
		g_gpu_device->OpenCommandList(cmd_list);
	}

	u64 SubmitCommandList(Commandlist cmd_list)
	{
		return g_gpu_device->CloseAndSubmitCommandList(cmd_list);
	}

	void BindConstantBuffer(GpuBuffer const* constant_buffer, ShaderStage::Enum stage, u8 slot)
	{
		ASSERT(slot < GPU_RESOURCE_HEAP_CBV_COUNT);
		g_gpu_device->BindConstantBuffer(constant_buffer, stage, slot);
	}

	void BindVertexBuffer(Commandlist cmd_list_handle, GpuBuffer const* vertex_buffer, u8 slot, u32 offset)
	{
		ID3D12GraphicsCommandList* command_list = g_gpu_device->HandleToCommandList(cmd_list_handle);

		D3D12_VERTEX_BUFFER_VIEW buffer_view;

		buffer_view.BufferLocation = vertex_buffer->resource->GetGPUVirtualAddress();
		buffer_view.SizeInBytes = vertex_buffer->desc.sizes_bytes;
		buffer_view.StrideInBytes = vertex_buffer->desc.stride_in_bytes;
		buffer_view.BufferLocation += static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(offset);
		buffer_view.SizeInBytes -= offset;

		command_list->IASetVertexBuffers(static_cast<u32>(slot), 1, &buffer_view);
	}

	void BindVertexBuffers(Commandlist cmd_list_handle, GpuBuffer const** vertex_buffers, u8 slot, u8 count, u32 const* offsets)
	{
		ID3D12GraphicsCommandList* command_list = g_gpu_device->HandleToCommandList(cmd_list_handle);

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

	void BindIndexBuffer(Commandlist cmd_list_handle, GpuBuffer const* index_buffer, u32 offset)
	{
		ID3D12GraphicsCommandList* command_list = g_gpu_device->HandleToCommandList(cmd_list_handle);

		D3D12_INDEX_BUFFER_VIEW buffer_view;
		buffer_view.BufferLocation = index_buffer->resource->GetGPUVirtualAddress() + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(offset);
		buffer_view.SizeInBytes = index_buffer->desc.sizes_bytes;
		buffer_view.Format = index_buffer->desc.format;

		command_list->IASetIndexBuffer(&buffer_view);
	}

	void CompileBasicPSOs()
	{
		g_pso_cache->CompileBasicPSOs();
	}

	GraphicsPSO CreateGraphicsPSO(D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc)
	{
		GraphicsPSO pso;
		pso.pso = g_gpu_device->CreateGraphicsPSO(desc).Detach();
		pso.desc = *desc;

		return pso;
	}

	void BindPSO(Commandlist cmd_list, BasicPSO::Enum basicPSOType)
	{
		BindPSO(cmd_list, g_pso_cache->GetBasicPSO(basicPSOType));
	}

	D3D12_PRIMITIVE_TOPOLOGY ConvertToPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE type)
	{
		switch (type)
		{
		case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
		{
			return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		}
		default:
		{
			ASSERT_FAIL_F("Unsupported primitive toplogy type!");
			return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		}
		}
	}

	void BindPSO(Commandlist cmd_list, PSO pso_handle)
	{
		ID3D12GraphicsCommandList* command_list = g_gpu_device->HandleToCommandList(cmd_list);
		GraphicsPSO const* pso = g_pso_cache->GetPSO(pso_handle);

		command_list->SetPipelineState(pso->pso);
		command_list->IASetPrimitiveTopology(ConvertToPrimitiveTopology(pso->desc.PrimitiveTopologyType));
	}

	GpuBuffer CreateVertexBuffer(Commandlist cmd_list_handle, void* vertex_data, u32 vertex_bytes, u32 vertex_stride_bytes)
	{
		ID3D12GraphicsCommandList* command_list = g_gpu_device->HandleToCommandList(cmd_list_handle);

		GpuBufferDesc desc;
		MemZeroSafe(desc);
		desc.usage = BufferUsage::Default;
		desc.sizes_bytes = vertex_bytes;
		desc.bind_flags = BindFlags::VertexBuffer;
		desc.stride_in_bytes = vertex_stride_bytes;

		GpuBuffer vertex_buffer = g_gpu_device->CreateBuffer(command_list, desc, vertex_data, L"VertexBuffer");

		TransitionBarrier(
			vertex_buffer.resource,
			cmd_list_handle,
			ResourceState::Copy_Dest,
			ResourceState::Vertex_And_Constant_Buffer);

		return vertex_buffer;
	}

	GpuBuffer CreateIndexBuffer(Commandlist cmd_list_handle, void* index_data, u32 index_bytes)
	{
		ID3D12GraphicsCommandList* command_list = g_gpu_device->HandleToCommandList(cmd_list_handle);

		GpuBufferDesc desc;
		MemZeroSafe(desc);
		desc.usage = BufferUsage::Default;
		desc.sizes_bytes = index_bytes;
		desc.bind_flags = BindFlags::IndexBuffer;
		desc.format = DXGI_FORMAT_R16_UINT;
		desc.stride_in_bytes = sizeof(u16);

		GpuBuffer index_buffer = g_gpu_device->CreateBuffer(command_list, desc, index_data, L"IndexBuffer");

		TransitionBarrier(
			index_buffer.resource,
			cmd_list_handle,
			ResourceState::Copy_Dest,
			ResourceState::Index_Buffer // ResourceState::Generic_Read?
		);

		return index_buffer;
	}

	GpuBuffer CreateBuffer(Commandlist cmd_list, GpuBufferDesc const& desc, wchar_t* name, void* initial_data)
	{
		ID3D12GraphicsCommandList* command_list = g_gpu_device->HandleToCommandList(cmd_list);

		GpuBuffer buffer = g_gpu_device->CreateBuffer(command_list, desc, initial_data, name);

		TransitionBarrier(
			buffer.resource,
			cmd_list,
			ResourceState::Copy_Dest,
			ResourceState::Vertex_And_Constant_Buffer
		);

		return buffer;
	}

	void UpdateBuffer(Commandlist cmd_list, GpuBuffer const* buffer, void* data, u32 size_bytes)
	{
		ASSERT_F(buffer->desc.usage != BufferUsage::Immutable, "Cannot update immutable buffer!");
		ASSERT_F(buffer->desc.sizes_bytes >= size_bytes, "Tried write data larger than buffer size!");

		size_bytes = min(size_bytes, buffer->desc.sizes_bytes);

		ID3D12GraphicsCommandList* command_list = g_gpu_device->HandleToCommandList(cmd_list);
		g_gpu_device->UpdateBuffer(command_list, buffer, data, size_bytes);
	}

	void DrawMesh(Commandlist cmd_list_handle, Mesh const* mesh)
	{
		BindVertexBuffer(cmd_list_handle, &mesh->vertex_buffer_gpu, 0, 0);
		BindIndexBuffer(cmd_list_handle, &mesh->index_buffer_gpu, 0);

		ID3D12GraphicsCommandList* command_list = g_gpu_device->HandleToCommandList(cmd_list_handle);
		
		// TODO(): can we hold off on this?
		g_gpu_device->UpdateConstantBindings(command_list);
		
		for (SubMesh const& submesh : mesh->submeshes)
		{
			command_list->DrawIndexedInstanced(submesh.num_indices, 1, submesh.first_index_location, submesh.base_vertex_location, 0);
		}
	}
}