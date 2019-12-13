#pragma once
#include "BaseApp.h"

class GpuDeviceDX12;

class MiniApp : public BaseApp
{
	virtual void Init() override;
	virtual bool Update() override;
	virtual void Exit() override;

private:
	GpuDeviceDX12* m_gpuDevice;
};