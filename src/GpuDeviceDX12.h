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

namespace Gfx
{
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
			Generic_Read = (((((0x1 | 0x2) | 0x40) | 0x80) | 0x200) | 0x800),
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
			VertexBuffer = 1 << 0,
			IndexBuffer = 1 << 2,
			ConstantBuffer = 1 << 3,
			ShaderResource = 1 << 4,
			StreamOutput = 1 << 5,
			RenderTarget = 1 << 6,
			DepthStencil = 1 << 7,
			UnorderedAccess = 1 << 8,
		};
	};

	struct ResourceFlags
	{
		enum Enum
		{
			AllowRawViews = 1 << 0,
			StructuredBuffer = 1 << 1,
			GenerateMips = 1 << 2,
			Shared = 1 << 3,
			TextureCube = 1 << 4,
			DrawIndirectArgs = 1 << 5,
			Tiled = 1 << 6
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

	struct GpuBuffer
	{
		GpuBufferDesc desc;
		ID3D12Resource* resource = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE srv;
		D3D12_CPU_DESCRIPTOR_HANDLE uav;
		D3D12_CPU_DESCRIPTOR_HANDLE cbv;
	};

	struct CpuBuffer
	{
		ComPtr<ID3DBlob> blob;
	};

	struct Commandlist
	{
		static const s32 INVALID_HANDLE = -1;

		s32 handle = INVALID_HANDLE;

		inline bool IsValid() const
		{
			return handle != INVALID_HANDLE;
		}
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
}

namespace Gfx
{
	struct InitFlags
	{
		enum Enum : u32
		{
			Enable_Debug_Layer = 1 << 0,
			Allow_Tearing = 1 << 1,
			Enabled_HDR = 1 << 2
		};
	};

	// Call these once from the main render thread.
	void CreateGpuDevice(void* main_window_handle, u32 flags);
	void DestroyGpuDevice();
	void RegisterCommandProducerThread();
	void BeginPresent();
	void EndPresent();

	// All of these APIs need to be thread-safe.
	Commandlist CreateCommandList(D3D12_COMMAND_LIST_TYPE type, wchar_t* name = nullptr);
	
	// Acquires a CommandAllocator and resets the CommandList to ready it for recording.
	void OpenCommandList(Commandlist cmd_list);

	// Closes the CommandList, free's up its allocator and pushes it directly into the graphics queue.
	u64 SubmitCommandList(Commandlist cmd_list);

	// TODO: This api is a much better alternative to internal queuing and flushing. Anything else is unsafe other than putting it in the
	// users hands or duplicating cmdlists and not allowing them to be reused throughout the frame.
	u64 SubmitCommandLists(Commandlist* cmd_list, u32 count);

	void WaitForFenceValueCpuBlocking(u64 fenceValue);
	void TransitionBarrier(ID3D12Resource* resources, Commandlist cmd_list, ResourceState::Enum stateBefore, ResourceState::Enum stateAfter);
	void TransitionBarriers(ID3D12Resource** resources, u8 numBarriers, Commandlist cmd_list, ResourceState::Enum stateBefore, ResourceState::Enum stateAfter);

	GpuBuffer CreateVertexBuffer(Commandlist cmd_list, void* vertex_data, u32 vertex_bytes, u32 vertex_stride_bytes);
	GpuBuffer CreateIndexBuffer(Commandlist cmd_list, void* index_data, u32 index_bytes);

	GpuBuffer CreateBuffer(Commandlist cmd_list, GpuBufferDesc const& desc, wchar_t* name, void* initial_data = nullptr);

	void BindVertexBuffer(Commandlist cmd_list, GpuBuffer const * vertex_buffer, u8 slot, u32 offset);
	void BindVertexBuffers(Commandlist cmd_list, GpuBuffer const ** vertex_buffers, u8 slot, u8 count, u32 const* offsets);
	void BindIndexBuffer(Commandlist cmd_list, GpuBuffer const* index_buffer, u32 offset);

	void DrawMesh(Mesh const* mesh, Commandlist cmd_list);
}
