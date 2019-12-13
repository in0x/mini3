#include "MiniApp.h"

#include "GpuDeviceDX12.h"
#include "InputMessageQueue.h"

void MiniApp::Init()
{
	__super::Init();

	m_gpuDevice = new GpuDeviceDX12();
	m_gpuDevice->Init(GetNativeHandle(), GpuDeviceDX12::IF_EnableDebugLayer | GpuDeviceDX12::IF_AllowTearing);
}

bool MiniApp::Update()
{
	__super::Update();

	InputMessages input = m_msgQueue->PumpMessages();
	
	m_gpuDevice->BeginPresent();
	// ...
	m_gpuDevice->EndPresent();
	
	return !input.m_bWantsToQuit;
}

void MiniApp::Exit()
{
	__super::Exit();

	m_gpuDevice->Flush();
	delete m_gpuDevice;
}