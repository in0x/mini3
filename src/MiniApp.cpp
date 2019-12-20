#include "MiniApp.h"

#include "GpuDeviceDX12.h"
#include "InputMessageQueue.h"
#include "GeoUtils.h"

void MiniApp::Init()
{
	__super::Init();

	RegisterCommandProducerThread();

	Gfx::CreateGpuDevice(GetNativeHandle(), GpuDeviceDX12::InitFlags::Enable_Debug_Layer | GpuDeviceDX12::InitFlags::Allow_Tearing);

	GeoUtils::CubeGeometry cube;
	GeoUtils::CreateBox(2.0f, 2.0f, 2.0f, &cube);
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

	Gfx::BeginPresent();
	// ...
	Gfx::EndPresent();
	
	return true;
}

void MiniApp::Exit()
{
	__super::Exit();
	Gfx::DestroyGpuDevice();
}