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
	Gfx::Commandlist m_upload_cmds;
	Gfx::GpuBuffer m_camera_constants;

	float3x4 m_view;
	float3x4 m_proj;
};