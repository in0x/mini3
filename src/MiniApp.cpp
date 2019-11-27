#include "MiniApp.h"

#include "DeviceResources.h"
#include "InputMessageQueue.h"

void MiniApp::Init()
{
	__super::Init();

	m_resources = new DeviceResources();
	m_resources->Init(GetNativeHandle(), DeviceResources::IF_EnableDebugLayer | DeviceResources::IF_AllowTearing);
}

bool MiniApp::Update()
{
	__super::Update();

	InputMessages input = m_msgQueue->PumpMessages();
	
	m_resources->BeginPresent();
	// ...
	m_resources->EndPresent();
	
	return !input.m_bWantsToQuit;
}

void MiniApp::Exit()
{
	__super::Exit();

	m_resources->Flush();
	delete m_resources;
}