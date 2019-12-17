#include "MiniApp.h"

#include "GpuDeviceDX12.h"
#include "InputMessageQueue.h"

struct SceneObject
{
	Mtx34 m_World;
	GraphicsPso m_pso;

};

void MiniApp::Init()
{
	__super::Init();

	m_gpu_device = new GpuDeviceDX12();
	m_gpu_device->Init(GetNativeHandle(), GpuDeviceDX12::ENABLED_DEBUG_LAYER | GpuDeviceDX12::ALLOW_TEARING);
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

	m_gpu_device->BeginPresent();
	// ...
	m_gpu_device->EndPresent();
	
	return true;
}

void MiniApp::Exit()
{
	__super::Exit();

	m_gpu_device->Flush();
	delete m_gpu_device;
}