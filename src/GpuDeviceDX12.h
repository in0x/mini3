#pragma once

#include "GfxTypes.h"
#include "BasicPSOs.h"

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
	void CreateGpuDevice(void* main_window_handle, u32 output_width, u32 output_height, u32 flags);
	void DestroyGpuDevice();
	void RegisterCommandProducerThread();
	void BeginPresent();
	void EndPresent();

	DXGI_FORMAT GetBackBufferFormat();
	DXGI_FORMAT GetDSFormat();

	// All of these APIs need to be thread-safe.
	Commandlist CreateCommandList(D3D12_COMMAND_LIST_TYPE type, wchar_t* name = nullptr);
	
	// Acquires a CommandAllocator and resets the CommandList to ready it for recording.
	void OpenCommandList(Commandlist cmd_list);

	// Explicit submit api is a much better alternative to internal queuing and flushing.Anything else is unsafe other than putting it in the
	// users hands or duplicating cmdlists and not allowing them to be reused throughout the frame.

	// Closes the CommandList, free's up its allocator and pushes it directly into the graphics queue.
	u64 SubmitCommandList(Commandlist cmd_list);

	// TODO(pw): Create plumbing to feed these into internal command queue. 
	u64 SubmitCommandLists(Commandlist* cmd_list, u32 count);

	void WaitForFenceValueCpuBlocking(u64 fence_value);
	void TransitionBarrier(ID3D12Resource* resources, Commandlist cmd_list, ResourceState::Enum state_before, ResourceState::Enum state_after);
	void TransitionBarriers(ID3D12Resource** resources, u8 numBarriers, Commandlist cmd_list, ResourceState::Enum state_before, ResourceState::Enum state_after);

	void CompileBasicPSOs();
	GraphicsPSO CreateGraphicsPSO(D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc);
	void BindPSO(Commandlist cmd_list, BasicPSO::Enum basicPSOType);
	void BindPSO(Commandlist cmd_list, PSO pso_handle);

	GpuBuffer CreateVertexBuffer(Commandlist cmd_list, void* vertex_data, u32 vertex_bytes, u32 vertex_stride_bytes);
	GpuBuffer CreateIndexBuffer(Commandlist cmd_list, void* index_data, u32 index_bytes);

	GpuBuffer CreateBuffer(Commandlist cmd_list, GpuBufferDesc const& desc, wchar_t* name, void* initial_data = nullptr);
	void UpdateBuffer(Commandlist cmd_list, GpuBuffer const* buffer, void* data, u32 size_bytes);

	void BindConstantBuffer(GpuBuffer const* constant_buffer, ShaderStage::Enum stage, u8 slot);

	void BindVertexBuffer(Commandlist cmd_list, GpuBuffer const* vertex_buffer, u8 slot, u32 offset);
	void BindVertexBuffers(Commandlist cmd_list, GpuBuffer const** vertex_buffers, u8 slot, u8 count, u32 const* offsets);
	void BindIndexBuffer(Commandlist cmd_list, GpuBuffer const* index_buffer, u32 offset);

	void DrawMesh(Commandlist cmd_list, Mesh const* mesh);
}
