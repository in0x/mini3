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
	Gfx::Commandlist m_present_cmds;
	Gfx::GpuBuffer m_camera_constants;
	
	Gfx::Mesh m_cube_mesh;

	mat44 m_world;
	mat44 m_view;
	mat44 m_proj;

	struct PerObjectData
	{
		mat44 model;
		mat44 view_proj;
		u8 pad[128]; // TODO(): do we/should we verify this inside cb creation? (padding to dx required alignment)
	};
};