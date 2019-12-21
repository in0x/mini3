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
	Gfx::Commandlist m_geo_upload_cmds;

	mtx34 m_view;
	mtx34 m_projection;
};