#pragma once
#include "BaseApp.h"
#include "Math.h"

class MiniApp : public BaseApp
{
	virtual void Init() override;
	virtual bool Update() override;
	virtual void Exit() override;

private:
	mtx34 m_view;
	mtx34 m_projection;
};