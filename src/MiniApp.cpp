#include "MiniApp.h"

#include "GpuDeviceDX12.h"
#include "InputMessageQueue.h"
#include "GeoUtils.h"

void MiniApp::Init()
{
	__super::Init();

	Gfx::RegisterCommandProducerThread();
	Gfx::CreateGpuDevice(GetNativeHandle(), Gfx::InitFlags::Enable_Debug_Layer | Gfx::InitFlags::Allow_Tearing);
	
	GeoUtils::CubeGeometry cube;
	GeoUtils::CreateBox(2.0f, 2.0f, 2.0f, &cube);

	m_geo_upload_cmds = Gfx::CreateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"geo_upload_cmds");
	Gfx::OpenCommandList(m_geo_upload_cmds);

	Gfx::Mesh mesh;
	u32 const vertex_size = sizeof(GeoUtils::Vertex);
	mesh.vertex_buffer_gpu = Gfx::CreateVertexBuffer(m_geo_upload_cmds, cube.vertices, vertex_size * GeoUtils::CubeGeometry::num_vertices, vertex_size);
	mesh.index_buffer_gpu = Gfx::CreateIndexBuffer(m_geo_upload_cmds, cube.indices, sizeof(GeoUtils::Index));

	Gfx::SubMesh* submesh = mesh.submeshes.PushBack();
	submesh->num_indices = cube.num_indices;
	submesh->base_vertex_location = 0;
	submesh->first_index_location = 0;

	u64 upload_fence = Gfx::SubmitCommandList(m_geo_upload_cmds);

	Gfx::WaitForFenceValueCpuBlocking(upload_fence);
}

bool MiniApp::Update()
{
	__super::Update();

	InputMessages input = m_msg_queue->PumpMessages();
	
	if (input.m_wants_to_quit)
	{
		// Don't start another frame if we want to quit 
		// and have stopped pumping the window thread.
		return false;
	}

	Gfx::BeginPresent();
	// ...
	Gfx::EndPresent();
	
	return true;
}

void MiniApp::Exit()
{
	__super::Exit();
	Gfx::DestroyGpuDevice();
}