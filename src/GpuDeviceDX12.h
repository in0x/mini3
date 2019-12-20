#pragma once

#include "Win32.h"

#include "d3dx12.h"

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

#include "Array.h"

using Microsoft::WRL::ComPtr;

static const u32 MAX_FRAME_COUNT = 2;

struct ResourceState
{
	enum Enum
	{
		Common = 0,
		Vertex_And_Constant_Buffer = 0x1,
		Index_Buffer = 0x2,
		Render_Target = 0x4,
		Unordered_Acces = 0x8,
		Depth_Write = 0x10,
		Depth_Read = 0x20,
		Non_Pixel_Shader_Resource = 0x40,
		Pixel_Shader_Resource = 0x80,
		Stream_Out = 0x100,
		Indirect_Argument = 0x200,
		Copy_Dest = 0x400,
		Copy_Source = 0x800,
		Resolve_Dest = 0x1000,
		Resolve_Source = 0x2000,
		Generic_Read = ( ( ( ( (0x1 | 0x2) | 0x40) | 0x80) | 0x200) | 0x800),
		Present = 0,
		Predication = 0x200,
		Video_Decode_Read = 0x10000,
		Video_Decode_Write = 0x20000,
		Video_Proecss_Read = 0x40000,
		Video_Process_Write = 0x80000
	};
};

struct ShaderStage
{
	enum Enum
	{
		Vertex,
		Pixel,
		Geometry,
		Hull,
		Domain,
		Compute,
		Invalid,
		Count = Invalid
	};
};

static_assert(ShaderStage::Vertex == 0, "Assuming ShaderStage 0 is Vertex!");

struct BufferUsage
{
	enum Enum
	{
		Default,
		Immutable,
		Dynamic,
		Staging
	};
};

struct BindFlags
{
	enum Enum
	{
		VertexBuffer	= 1 << 0,
		IndexBuffer		= 1 << 2,
		ConstantBuffer	= 1 << 3,
		ShaderResource	= 1 << 4,
		StreamOutput	= 1 << 5,
		RenderTarget	= 1 << 6,
		DepthStencil	= 1 << 7,
		UnorderedAccess = 1 << 8,
	};
};

struct ResourceFlags
{
	enum Enum
	{
		AllowRawViews	 = 1 << 0,
		StructuredBuffer = 1 << 1,
		GenerateMips	 = 1 << 2,
		Shared			 = 1 << 3,
		TextureCube		 = 1 << 4,
		DrawIndirectArgs = 1 << 5,
		Tiled			 = 1 << 6
	};
};

struct Shader
{
	ComPtr<ID3DBlob> blob = nullptr;
};

struct GraphicsPsoDesc
{
	Shader shaders[ShaderStage::Count];
};

struct GraphicsPso
{
	ComPtr<ID3D12PipelineState> pso = nullptr;
};

struct GpuResource
{
	ComPtr<ID3D12Resource> resource = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE srv;
	D3D12_CPU_DESCRIPTOR_HANDLE uav;
};

struct GpuBufferDesc
{
	u32 sizes_bytes = 0;
	u32 bind_flags = 0;
	u32 cpu_access_flags = 0;
	u32 misc_flags = 0;
	u32 stride_in_bytes = 0;
	BufferUsage::Enum usage = BufferUsage::Default;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

struct GpuBuffer : public GpuResource
{
	D3D12_CPU_DESCRIPTOR_HANDLE cbv;
	GpuBufferDesc desc;
};

struct CpuBuffer
{
	ComPtr<ID3DBlob> blob;
};

struct SubMesh
{
	u32 num_indices = 0;
	u32 first_index_location = 0;
	u32 base_vertex_location = 0;
};

struct Mesh
{
	// When would I need these other than special cases like deformation maybe?
	CpuBuffer* vertex_buffer_cpu = nullptr;
	CpuBuffer* index_buffer_cpu = nullptr;

	GpuBuffer vertex_buffer_gpu;
	GpuBuffer index_buffer_gpu;

	Array<SubMesh, 8> submeshes;
};

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
	void BindDescriptor(ShaderStage::Enum stage, u32 offset, D3D12_CPU_DESCRIPTOR_HANDLE const* descriptor, ID3D12Device* device);
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
	bool m_is_dirty[ShaderStage::Count];
	D3D12_CPU_DESCRIPTOR_HANDLE const** m_bound_descriptors = nullptr;
};

class ResourceAllocator
{
public:
	void Create(ID3D12Device* device, size_t size_bytes);
	void Clear();

	u8* Allocate(size_t size_bytes, size_t alignment);
	u64 CalculateOffset(u8* address);

	inline ID3D12Resource* GetRootBuffer() { return m_resource.Get(); }

private:
	ComPtr<ID3D12Resource> m_resource = nullptr;
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

void RegisterCommandProducerThread();

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

	void OpenCommandList(ID3D12GraphicsCommandList* cmdlist, CommandAllocator* allocator);
	void CloseCommandList(ID3D12GraphicsCommandList* cmdlist);
	bool EnsureAllCommandListsClosed();

private:
	ID3D12GraphicsCommandList* m_active_gfx_lists[NUM_MAX_THREADS];
	ID3D12GraphicsCommandList* m_active_copy_lists[NUM_MAX_THREADS];
	ID3D12GraphicsCommandList* m_active_compute_lists[NUM_MAX_THREADS];
};

struct FrameResource
{
	u64 end_of_frame_fence;

	CommandAllocator cmd_allocator;
	CommandListManager cmd_list_manager;
	ComPtr<ID3D12Resource> render_target;
	ComPtr<ID3D12GraphicsCommandList> command_list;

	DescriptorTableFrameAllocator resource_descriptors_gpu;
	DescriptorTableFrameAllocator sampler_descriptors_gpu;

	ResourceAllocator transient_resource_allocator;

	std::mutex submit_lock;
	
	Array<ID3D12GraphicsCommandList*, NUM_MAX_THREADS, AtomicCounterPolicy> gfx_queue;

	void CloseCommandList(ID3D12GraphicsCommandList* cmd_list)
	{
		cmd_list_manager.CloseCommandList(cmd_list);
	}

	void CloseAndSubmitCommandList(ID3D12GraphicsCommandList* cmd_list)
	{
		cmd_list_manager.CloseCommandList(cmd_list);
	
		ScopedLock lock(submit_lock);
		gfx_queue.PushBack(cmd_list);
	}

	void SubmitQueuedWork(CommandQueue* queue)
	{
		ScopedLock lock(submit_lock);

		end_of_frame_fence = queue->ExecuteCommandLists((ID3D12CommandList**)gfx_queue.Data(), gfx_queue.Size());
		gfx_queue.Clear();
	}

	inline ID3D12CommandAllocator* GetGfxCmdAllocator()
	{
		return cmd_allocator.GetAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT);
	}

	inline ID3D12CommandAllocator* GetComputeCmdAllocator()
	{
		return cmd_allocator.GetAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE);
	}

	inline ID3D12CommandAllocator* GetCopyCmdAllocator()
	{
		return cmd_allocator.GetAllocator(D3D12_COMMAND_LIST_TYPE_COPY);
	}

	inline ID3D12GraphicsCommandList* GetFrameCmdList()
	{
		return command_list.Get();
	}
};

class GpuDeviceDX12
{
public:
	struct InitFlags
	{
		enum Enum : u32
		{
			Enable_Debug_Layer = 1 << 0,
			Allow_Tearing = 1 << 1,
			Enabled_HDR = 1 << 2
		};
	};

	void Init(void* windowHandle, u32 initFlags);
	void Destroy();
	bool IsTearingAllowed();

	void BeginPresent();
	void EndPresent();
	void MoveToNextFrame();
	void Flush();

	// TODO this isnt quite threadsafe yet. (Initial Allocator might need to be abstracted)
	GpuBuffer CreateBuffer(GpuBufferDesc const& desc, void* initial_data, u32 initial_data_bytes);
	
	inline ID3D12Device* GetD3DDevice() const { return m_d3d_device.Get(); }

	ComPtr<ID3D12GraphicsCommandList> CreateCommandList(D3D12_COMMAND_LIST_TYPE type, wchar_t* name = nullptr);

	void OpenCommandList(ID3D12GraphicsCommandList* cmd_list);

	void CloseAndEnqueueCommandList(ID3D12GraphicsCommandList* cmd_list);

	u64 CloseAndSubmitCommandList(ID3D12GraphicsCommandList* cmd_list);

	void FlushQueuedGraphicsWork();
	
	inline ID3D12GraphicsCommandList* GetCurrentCommandList() { return m_frame_resource[m_frame_index].GetFrameCmdList(); }

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

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView() const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const;

	inline IDXGISwapChain3*           GetSwapChain() const { return m_swap_chain.Get(); }
	inline D3D12_VIEWPORT             GetScreenViewport() const { return m_screen_viewport; }
	inline D3D12_RECT                 GetScissorRect() const { return m_scissor_rect; }
	inline u32	                      GetCurrentFrameIndex() const { return m_frame_index; }

	inline ID3D12Resource*			  GetCurrentRenderTarget() const { return m_frame_resource[m_frame_index].render_target.Get(); }

	u32									m_frame_index;
	u32									m_init_flags;
	u32									m_backbuffer_width;
	u32									m_backbuffer_height;

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
	inline FrameResource* GetFrameResources() { return m_frame_resource + m_frame_index; }
	void createFrameResources();
	void destroyFrameResource();

	DescriptorAllocator m_rt_alloctor;
	DescriptorAllocator m_ds_allocator;
	DescriptorAllocator m_resource_allocator;
	DescriptorAllocator m_sampler_allocator;

	ResourceAllocator m_buffer_upload_allocator;
	ResourceAllocator m_texture_upload_allocator;
};

namespace Gfx
{
	// Call these once from the main render thread.
	void CreateGpuDevice(void* main_window_handle, u32 flags);
	void DestroyGpuDevice();

	// All of these APIs need to be thread-safe.
	void BeginPresent();
	void EndPresent();

	ComPtr<ID3D12GraphicsCommandList> CreateCommandList(D3D12_COMMAND_LIST_TYPE type, wchar_t* name = nullptr);
	
	// Acquires a CommandAllocator and resets the CommandList to ready it for recording.
	void OpenCommandList(ID3D12GraphicsCommandList* cmd_list);

	// Closes the CommandList, free's up its allocator and pushes it into the submit queue, which will push the work
	// into the graphics queue on the next submit.
	void EnqueueCommandList(ID3D12GraphicsCommandList* cmd_list);
	
	// Closes the CommandList, free's up its allocator and pushes it directly into the graphics queue.
	void SubmitCommandList(ID3D12GraphicsCommandList* cmd_list);

	// Flushes the enqueued command lists out to the graphics queue.
	void FlushQueuedGraphicsWork();

	u64 Signal(ID3D12CommandQueue* commandQueue, ID3D12Fence* fence, u64 fenceValue);
	void WaitForFenceValue(ID3D12Fence* fence, uint64_t fenceValue, HANDLE fenceEvent, u32 durationMS = INFINITE);
	void TransitionBarrier(ID3D12Resource* resources, ID3D12GraphicsCommandList* cmd_list, ResourceState::Enum stateBefore, ResourceState::Enum stateAfter);
	void TransitionBarriers(ID3D12Resource** resources, u8 numBarriers, ID3D12GraphicsCommandList* cmd_list, ResourceState::Enum stateBefore, ResourceState::Enum stateAfter);

	void BindVertexBuffer(ID3D12GraphicsCommandList* command_list, GpuBuffer const * vertex_buffer, u8 slot, u32 offset);
	void BindVertexBuffers(ID3D12GraphicsCommandList* command_list, GpuBuffer const ** vertex_buffers, u8 slot, u8 count, u32 const* offsets);
	void BindIndexBuffer(ID3D12GraphicsCommandList* command_list, GpuBuffer const* index_buffer, u32 offset);

	void DrawMesh(Mesh const* mesh, ID3D12GraphicsCommandList* command_list);
}
