#pragma once
#include "BaseApp.h"
#include "Math.h"

class GpuDeviceDX12;

class MiniApp : public BaseApp
{
	virtual void Init() override;
	virtual bool Update() override;
	virtual void Exit() override;

private:
	Mtx34 m_view;
	Mtx34 m_projection;

	GpuDeviceDX12* m_gpu_device;
};