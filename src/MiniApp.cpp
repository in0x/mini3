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
	GeoUtils::CreateBox(1.5f, 1.5f, 1.5f, &cube);

	m_upload_cmds = Gfx::CreateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"geo_upload_cmds");
	Gfx::OpenCommandList(m_upload_cmds);

	m_draw_cmds = Gfx::CreateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"draw_cmds");

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

	// TODO(pw): Finished implementing geomtry upload. Next we need to write shaders and PSOs so we can start binding
	// them and render the geometry. We also need fill out the data that goes into our camera constant buffer.
}

void MiniApp::render()
{
	Gfx::OpenCommandList(m_draw_cmds);

	Gfx::BindPSO(m_draw_cmds, Gfx::BasicPSO::VertexColorSolid);
	Gfx::BindConstantBuffer(&m_camera_constants, Gfx::ShaderStage::Vertex, 0);

	//mtx4x4 mvp = Math::MatrixMul(m_proj, Math::MatrixMul(m_view, m_world));
	//mtx4x4 mvp = Math::MatrixMul(Math::MatrixMul(m_world, m_view), m_proj);
	//mvp = Math::MatrixTranspose(mvp);

	PerObjectData cb_data;
	//cb_data.model_view_proj = mvp;
	cb_data.model = Math::MatrixTranspose(m_world);
	cb_data.view_proj = Math::MatrixTranspose(Math::MatrixMul(m_view, m_proj));

	//cb_data.model = (m_world);
	//cb_data.view =  (m_view);
	//cb_data.proj =  (m_proj);

	// TODO(): Surely I should be able to record this into upload_cmds, then submit and make draw_cmds wait on the fence.
	Gfx::UpdateBuffer(m_draw_cmds, &m_camera_constants, &cb_data, sizeof(cb_data));
	Gfx::DrawMesh(m_draw_cmds, &m_cube_mesh);
	Gfx::SubmitCommandList(m_draw_cmds);
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

	f32 total_time = GetTotalTimeS(m_timer);

	// Calc single object world matrix
	{
		f32 angle = sin(total_time);

		mtx4x4 scale = Math::MatrixScale(1.0f, 1.0f, 1.0f);
		mtx4x4 translate = Math::MatrixTranslation(0.0f, 0.f, 10.0f);
		mtx4x4 rotation = Math::MatrixRotationY(angle);
		//m_world = Math::MatrixMul(rotation, translate);
		//m_world = Math::MatrixMul(translate, Math::MatrixMul(rotation, scale));
		//m_world = Math::MatrixMul(scale, translate);
		DirectX::XMStoreFloat4x4(&m_world, DirectX::XMMatrixIdentity());
	}

	// Calc view matrix
	{
		vec3 eye_pos = vec3(0.0f, 0.0f, -10.0f);
		vec3 look_at = vec3(0.0f, 0.0f, 0.0f);
		vec3 up = Math::UpDir();

		m_view = Math::MatrixLookAtLH(eye_pos, look_at, up);
	}

	// Calc proj mat
	{
		// TODO(): pass this in via window config (and update on resize)
		f32 aspect_ratio = 800.0f / 600.0f;
		f32 fov_y = Math::DegreeToRad(70.0f);

		m_proj = Math::MatrixPerspectiveFovLH(fov_y, aspect_ratio, 0.01f, 1000.0f);

		//m_proj = Math::MatrixPerspectiveFovLH(fov_y * aspect_ratio * 0.01f, aspect_ratio * 0.01f, 0.01f, 1000.0f);

		//DirectX::XMMATRIX persp = DirectX::XMMatrixPerspectiveLH(800.f, 600.f, 0.01f, 1000.0f);
		//DirectX::XMStoreFloat4x4(&m_proj, persp);
	}

	Gfx::BeginPresent();
	render();
	Gfx::EndPresent();
	
	return true;
}

void MiniApp::Exit()
{
	__super::Exit();
	Gfx::DestroyGpuDevice();
}