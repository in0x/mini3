#include "MiniApp.h"

#include "GpuDeviceDX12.h"
#include "WindowConfig.h"
#include "InputMessageQueue.h"
#include "GeoUtils.h"

void MiniApp::Init()
{
	__super::Init();

#ifdef _DEBUG
	u32 gfx_flags = Gfx::InitFlags::Enable_Debug_Layer | Gfx::InitFlags::Allow_Tearing;
#else
	u32 gfx_flags = Gfx::InitFlags::Allow_Tearing;
#endif

	Gfx::CreateGpuDevice(GetNativeHandle(), m_window_cfg->width, m_window_cfg->height, gfx_flags);
	
	GeoUtils::CubeGeometry cube;
	GeoUtils::CreateBox(1.5f, 1.5f, 1.5f, &cube);

	m_upload_cmds = Gfx::CreateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"geo_upload_cmds");
	Gfx::OpenCommandList(m_upload_cmds);

	m_draw_cmds = Gfx::CreateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"draw_cmds");
	m_present_cmds = Gfx::CreateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"present_cmds");

	u32 const vertex_size = sizeof(GeoUtils::Vertex);
	u32 const index_size = sizeof(GeoUtils::Index);
	m_cube_mesh.vertex_buffer_gpu = Gfx::CreateVertexBuffer(m_upload_cmds, cube.vertices, vertex_size * GeoUtils::CubeGeometry::num_vertices, vertex_size);
	m_cube_mesh.index_buffer_gpu = Gfx::CreateIndexBuffer(m_upload_cmds, cube.indices, index_size * GeoUtils::CubeGeometry::num_indices);

	Gfx::SubMesh* submesh = m_cube_mesh.submeshes.PushBack();
	submesh->num_indices = cube.num_indices;
	submesh->base_vertex_location = 0;
	submesh->first_index_location = 0;

	PerObjectData cbData;

	Gfx::GpuBufferDesc cam_desc;
	cam_desc.bind_flags = Gfx::BindFlags::ConstantBuffer;
	cam_desc.usage = Gfx::BufferUsage::Default;
	cam_desc.cpu_access_flags = 0;
	cam_desc.sizes_bytes = sizeof(PerObjectData);

	m_camera_constants = Gfx::CreateBuffer(m_upload_cmds, cam_desc, L"CameraConstants", &cbData);
	u64 upload_fence = Gfx::SubmitCommandList(m_upload_cmds);

	Gfx::CompileBasicPSOs();
	Gfx::WaitForFenceValueCpuBlocking(upload_fence);
}

void MiniApp::render()
{
	Gfx::BeginPresent(m_present_cmds);
	Gfx::OpenCommandList(m_draw_cmds);

	// TODO(): Surely I should be able to record this into upload_cmds, then submit and make draw_cmds wait on the fence.
	PerObjectData cb_data;
	cb_data.model = m_world;
	cb_data.view_proj = m_proj * m_view;
	Gfx::UpdateBuffer(m_draw_cmds, &m_camera_constants, &cb_data, sizeof(cb_data));

	Gfx::BindPSO(m_draw_cmds, Gfx::BasicPSO::VertexColorSolid);
	Gfx::BindConstantBuffer(&m_camera_constants, Gfx::ShaderStage::Vertex, 0);

	Gfx::DrawMesh(m_draw_cmds, &m_cube_mesh);
	Gfx::SubmitCommandList(m_draw_cmds);

	Gfx::EndPresent(m_present_cmds);
}

bool IsKeyDown(InputMessages const* msg, KeyCode::Enum key)
{
	for (u32 i = msg->m_keys.Size(); i--;)
	{
		if (msg->m_keys[i].m_key == key)
		{
			return true;
		}
	}

	return false;
}

bool MiniApp::Update()
{
	__super::Update();

	InputMessages input = m_msg_queue->PumpMessages();
	
	if (input.m_wants_to_quit)
	{
		Gfx::Flush();

		// Don't start another frame if we want to quit 
		// and have stopped pumping the window thread.
		return false;
	}

	//LOG(Log::Category::Default, "KeyStates:");
	//LOG(Log::Category::Default, "W: %s", IsKeyDown(&input, KeyCode::W) ? "yes" : "no");
	//LOG(Log::Category::Default, "A: %s", IsKeyDown(&input, KeyCode::A) ? "yes" : "no");
	//LOG(Log::Category::Default, "S: %s", IsKeyDown(&input, KeyCode::S) ? "yes" : "no");
	//LOG(Log::Category::Default, "D: %s", IsKeyDown(&input, KeyCode::D) ? "yes" : "no");

	f32 total_time = GetTotalTimeS(m_timer);

	// Calc single object world matrix
	{
		f32 angle = sin(total_time);

		mat44 translate = Math::Translation<mat44>(0.0f, 0.0f, 1.0f);
		mat44 rotation = Math::RotationX<mat44>(angle);

		m_world = translate * rotation;
	}

	// Calc view matrix
	{
		vec3 eye_pos = vec3(0.0f, 0.0f, -5.0f);
		vec3 look_at = vec3(0.0f, 0.0f, 0.0f);
		vec3 up = Math::UpDir();

		m_view = Math::MatrixLookAtLH(eye_pos, look_at, up);
	}

	// Calc proj mat
	{
		f32 aspect_ratio = (f32)m_window_cfg->width / (f32)m_window_cfg->height;
		f32 fov_y = Math::DegreeToRad(70.0f);

		m_proj = Math::MatrixPerspectiveFovLH(fov_y, aspect_ratio, 0.01f, 1000.0f);
	}

	render();
	
	return true;
}

void MiniApp::Exit()
{
	__super::Exit();
	Gfx::DestroyGpuDevice();
}