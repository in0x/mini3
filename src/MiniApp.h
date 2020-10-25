#pragma once
#include "BaseApp.h"
#include "Math.h"
#include "GpuDeviceDX12.h"

class MiniApp : public BaseApp
{
	virtual void Init() override;
	virtual bool Update() override;
	virtual void Exit() override;

private:
	void render();

	Gfx::Commandlist m_upload_cmds;
	Gfx::Commandlist m_draw_cmds;
	Gfx::GpuBuffer m_camera_constants;
	
	Gfx::Mesh m_cube_mesh;

	mtx4x4 m_world;
	mtx4x4 m_view;
	mtx4x4 m_proj;

	struct PerObjectData
	{
		mtx4x4 model;
		mtx4x4 view_proj;
		u8 pad[128];
	};
};