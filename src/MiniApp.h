#pragma once
#include "BaseApp.h"

class DeviceResources;

class MiniApp : public BaseApp
{
	virtual void Init() override;
	virtual void Update() override;
	virtual void Render() override;
	virtual void Exit() override;

private:
	DeviceResources* m_resources;
};