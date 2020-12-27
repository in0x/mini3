#include "MiniApp.h"

#include "GpuDeviceDX12.h"
#include "WindowConfig.h"
#include "InputMessageQueue.h"
#include "GeoUtils.h"

#include "GLTFImport.h"

static void CreateCubeMesh(Gfx::Commandlist cmds, Gfx::Mesh* out_mesh)
{
	GeoUtils::CubeGeometry cube;
	GeoUtils::CreateBox(1.5f, 1.5f, 1.5f, &cube);

	u32 const position_size = sizeof(Gfx::Position_t);
	u32 const normal_size =   sizeof(Gfx::Normal_t);
	u32 const texcoord_size = sizeof(Gfx::TexCoord_t);
	u32 const index_size =    sizeof(Gfx::Index_t);

	out_mesh->vertex_attribs_gpu[Gfx::VertexAttribType::Position] = Gfx::CreateVertexBuffer(
		cmds, cube.position, position_size * GeoUtils::CubeGeometry::num_vertices, position_size);

	out_mesh->vertex_attribs_gpu[Gfx::VertexAttribType::Normal] = Gfx::CreateVertexBuffer(
		cmds, cube.normal, normal_size * GeoUtils::CubeGeometry::num_vertices, normal_size);

	out_mesh->vertex_attribs_gpu[Gfx::VertexAttribType::TexCoord] = Gfx::CreateVertexBuffer(
		cmds, cube.texcoord, texcoord_size * GeoUtils::CubeGeometry::num_vertices, texcoord_size);

	out_mesh->index_buffer_gpu = Gfx::CreateIndexBuffer(cmds, cube.indices, index_size * GeoUtils::CubeGeometry::num_indices);

	Gfx::SubMesh* submesh = out_mesh->submeshes.PushBack();
	submesh->num_indices = cube.num_indices;
	submesh->base_vertex_location = 0;
	submesh->first_index_location = 0;
}

static void UploadMeshImport(Gfx::Commandlist cmds, Mini::MeshImport const* imported, Gfx::Mesh* out_mesh)
{
	u32 const position_size = sizeof(Gfx::Position_t);
	u32 const normal_size = sizeof(Gfx::Normal_t);
	u32 const texcoord_size = sizeof(Gfx::TexCoord_t);
	u32 const index_size = sizeof(Gfx::Index_t);

	out_mesh->vertex_attribs_gpu[Gfx::VertexAttribType::Position] = Gfx::CreateVertexBuffer(
		cmds, imported->position_buffer, position_size * imported->num_vertices, position_size);

	out_mesh->vertex_attribs_gpu[Gfx::VertexAttribType::Normal] = Gfx::CreateVertexBuffer(
		cmds, imported->normal_buffer, normal_size * imported->num_vertices, normal_size);

	out_mesh->vertex_attribs_gpu[Gfx::VertexAttribType::TexCoord] = Gfx::CreateVertexBuffer(
		cmds, imported->texcoord_buffer, texcoord_size * imported->num_vertices, texcoord_size);

	out_mesh->index_buffer_gpu = Gfx::CreateIndexBuffer(cmds, imported->index_buffer, index_size * imported->num_indices);

	Gfx::SubMesh* submesh = out_mesh->submeshes.PushBack();
	submesh->num_indices = imported->num_indices;
	submesh->base_vertex_location = 0;
	submesh->first_index_location = 0;
}

void MiniApp::Init()
{
	__super::Init();

	Memory::Arena import_scratch;
	Memory::InitArena(&import_scratch, Megabyte(16)); // Just reuse a larger game memory arena 

	Memory::Arena mesh_resource_memory;
	Memory::InitArena(&mesh_resource_memory, Megabyte(128));

	Mini::SceneImporter importer;
	importer.file_path = "C:\\Users\\Philipp\\Documents\\work\\glTF-Sample-Models\\2.0\\DamagedHelmet\\glTF\\DamagedHelmet.gltf";
	importer.scratch_memory = &import_scratch;
	importer.mesh_memory = &mesh_resource_memory;

	Mini::MeshImport mesh_data = Mini::Import(&importer);
	
	ON_SCOPE_EXIT(Memory::FreeArena(&import_scratch));
	ON_SCOPE_EXIT(Memory::FreeArena(&mesh_resource_memory));

#ifdef _DEBUG
	u32 gfx_flags = Gfx::InitFlags::Enable_Debug_Layer | Gfx::InitFlags::Allow_Tearing;
#else
	u32 gfx_flags = Gfx::InitFlags::Allow_Tearing;
#endif

	Gfx::CreateGpuDevice(GetNativeHandle(), m_window_cfg->width, m_window_cfg->height, gfx_flags);

	// Create command lists
	{
		m_upload_cmds = Gfx::CreateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"geo_upload_cmds");
		m_draw_cmds = Gfx::CreateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"draw_cmds");
		m_present_cmds = Gfx::CreateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"present_cmds");
	}

	Gfx::OpenCommandList(m_upload_cmds);
	CreateCubeMesh(m_upload_cmds, &m_cube_mesh);
	UploadMeshImport(m_upload_cmds, &mesh_data, &m_import_mesh);

	// Generate per-frame and per-object constant buffers
	{
		Gfx::GpuBufferDesc frame;
		frame.bind_flags = Gfx::BindFlags::ConstantBuffer;
		frame.usage = Gfx::BufferUsage::Default;
		frame.cpu_access_flags = 0;
		frame.sizes_bytes = sizeof(PerFrameData);
		m_frame_constants = Gfx::CreateBuffer(m_upload_cmds, frame, L"FrameConstants");

		Gfx::GpuBufferDesc obj;
		obj.bind_flags = Gfx::BindFlags::ConstantBuffer;
		obj.usage = Gfx::BufferUsage::Dynamic;
		obj.cpu_access_flags = 0;
		obj.sizes_bytes = sizeof(PerObjectData);
		m_obj_constants = Gfx::CreateBuffer(m_upload_cmds, obj, L"ObjectConstants");
	}

	u64 upload_fence = Gfx::SubmitCommandList(m_upload_cmds);

	Gfx::CompileBasicPSOs();
	Gfx::WaitForFenceValueCpuBlocking(upload_fence);
}

void MiniApp::render()
{
	Gfx::BeginPresent(m_present_cmds);
	Gfx::OpenCommandList(m_draw_cmds);

	PerFrameData frame_constants;
	frame_constants.view_proj = m_proj * m_view;
	Gfx::UpdateBuffer(m_draw_cmds, &m_frame_constants, &frame_constants, sizeof(frame_constants));

	// TODO(): Surely I should be able to record this into upload_cmds, then submit and make draw_cmds wait on the fence.
	PerObjectData obj_constants;
	obj_constants.model = m_world;
	Gfx::UpdateBuffer(m_draw_cmds, &m_obj_constants, &obj_constants, sizeof(obj_constants));

	Gfx::BindPSO(m_draw_cmds, Gfx::BasicPSO::VertexColorSolid);
	Gfx::BindConstantBuffer(&m_frame_constants, Gfx::ShaderStage::Vertex, 0);
	Gfx::BindConstantBuffer(&m_obj_constants, Gfx::ShaderStage::Vertex, 1);

	Gfx::DrawMesh(m_draw_cmds, &m_import_mesh);

	Gfx::SubmitCommandList(m_draw_cmds);

	Gfx::EndPresent(m_present_cmds);
}

struct ArcBallCamera
{
	vec3 m_eye_pos;

	f32 m_phi = Math::Pi / 4.0f;
	f32 m_zoom = 5.0f;
	f32 m_theta =  Math::Pi * 1.5f;

	f32 m_last_mouse_x;
	f32 m_last_mouse_y;
};

void ProcessCameraInput(InputMessages const* input, ArcBallCamera* camera)
{
	u32 const num_inputs = input->m_keys.Size();
	for (u32 i = 0; i < num_inputs; ++i)
	{
		KeyMsg const& msg = input->m_keys[i];

		switch (msg.m_type)
		{
		case KeyMsg::KeyDown:
		case KeyMsg::KeyUp:
		{
			camera->m_last_mouse_x = msg.m_data.m_mouse_pos.x;
			camera->m_last_mouse_y = msg.m_data.m_mouse_pos.y;
			break;
		}
		case KeyMsg::MouseMove:
		{
			if (msg.m_key == KeyCode::MSB_LEFT)
			{
				f32 dx = msg.m_data.m_mouse_pos.x - camera->m_last_mouse_x;
				f32 dy = msg.m_data.m_mouse_pos.y - camera->m_last_mouse_y;

				camera->m_theta -= Math::DegreeToRad(0.2f * dx);
				camera->m_phi += Math::DegreeToRad(0.2f * dy);
			}
			else if (msg.m_key == KeyCode::MSB_RIGHT)
			{
				f32 dx = msg.m_data.m_mouse_pos.x - camera->m_last_mouse_x;
				f32 dy = msg.m_data.m_mouse_pos.y - camera->m_last_mouse_y;

				camera->m_zoom += (0.01f * (dx - dy));
			}
			
			camera->m_last_mouse_x = msg.m_data.m_mouse_pos.x;
			camera->m_last_mouse_y = msg.m_data.m_mouse_pos.y;

			break;
		}
		case KeyMsg::MouseWheel:
		{
			camera->m_zoom -= 0.005f * msg.m_data.wheel_delta;
		}
		}
	}
}

void EvaluateCameraMatrices(ArcBallCamera* camera)
{
	camera->m_phi = Clamp(camera->m_phi, 0.1f, Math::Pi - 0.1f); // NOTE(): Restrict to ~+-180°

	camera->m_eye_pos.x = camera->m_zoom * sinf(camera->m_phi) * cosf(camera->m_theta);
	camera->m_eye_pos.z = camera->m_zoom * sinf(camera->m_phi) * sinf(camera->m_theta);
	camera->m_eye_pos.y = camera->m_zoom * cosf(camera->m_phi);
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

	f32 total_time = GetTotalTimeS(m_timer);

	// Calc single object world matrix
	{
		f32 angle = sin(total_time);

		mat44 translate = Math::Translation<mat44>(0.0f, 0.0f, 0.0f);
		mat44 rotation = Math::RotationXYZ<mat44>(Math::Rad(-0.7f), Math::Rad(0.0f), Math::Rad(0.0f));

		m_world = translate * rotation;
	}

	static ArcBallCamera s_camera;
	ProcessCameraInput(&input, &s_camera);
	EvaluateCameraMatrices(&s_camera);

	// Calc view matrix
	{
		vec3 eye_pos = s_camera.m_eye_pos;
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