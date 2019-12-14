#pragma once

#include "Win32.h"

#include "d3dx12.h"

#if defined(NTDDI_WIN10_RS2)
#define USES_DXGI6 1
#else
#define USES_DXGI6 0
#endif

#if USES_DXGI6
#include <dxgi1_6.h>
#else
#include <dxgi1_5.h>
#endif

#include <dxgidebug.h>

using Microsoft::WRL::ComPtr;

static const u32 MAX_FRAME_COUNT = 2;

class GpuDeviceDX12
{
public:
	enum InitFlags : u32
	{
		IF_EnableDebugLayer = 1 << 0,
		IF_AllowTearing = 1 << 1,
		IF_EnableHDR = 1 << 2
	};

	void Init(void* windowHandle, u32 initFlags);
	bool IsTearingAllowed();

	void BeginPresent();
	void EndPresent();
	void Flush();
	u64 Signal(ID3D12CommandQueue* commandQueue, ID3D12Fence* fence, u64 fenceValue);
	void WaitForFenceValue(ID3D12Fence* fence, uint64_t fenceValue, HANDLE fenceEvent, u32 durationMS = INFINITE);

	inline ID3D12Device*              GetD3DDevice() const { return m_d3dDevice.Get(); }
	inline IDXGISwapChain3*           GetSwapChain() const { return m_swapChain.Get(); }
	inline u64						  GetCurrentFenceValue() const { return m_fenceValue; }
	inline ID3D12Resource*            GetCurrentRenderTarget() const { return m_renderTargets[m_frameIndex].Get(); }
	inline ID3D12CommandQueue*        GetCommandQueue() const { return m_commandQueue.Get(); }
	inline ID3D12CommandAllocator*    GetCommandAllocator() const { return m_commandAllocators[m_frameIndex].Get(); }
	inline ID3D12GraphicsCommandList* GetCommandList() const { return m_commandList.Get(); }
	inline ID3D12Fence*				  GetFence() const { return m_fence.Get(); }
	inline HANDLE					  GetFenceEvent() { return m_fenceEvent.Get(); }
	inline D3D12_VIEWPORT             GetScreenViewport() const { return m_screenViewport; }
	inline D3D12_RECT                 GetScissorRect() const { return m_scissorRect; }
	inline UINT                       GetCurrentFrameIndex() const { return m_frameIndex; }	

private:
	void enableDebugLayer();
	bool checkTearingSupport();
	void createDevice(bool bEnableDebugLayer);
	void checkFeatureLevel();
	void createCommandQueue();
	void createDescriptorHeaps();
	void createCommandAllocators();
	void createCommandList();
	void createEndOfFrameFence();

	void initWindowSizeDependent();
	void resizeSwapChain(u32 width, u32 height, DXGI_FORMAT format);
	void createSwapChain(u32 width, u32 height, DXGI_FORMAT format);
	void updateColorSpace();
	void createBackBuffers();
	void createDepthBuffer(u32 width, u32 height);

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView() const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const;

	u32									m_frameIndex;

	ComPtr<ID3D12Device>                m_d3dDevice;
	ComPtr<ID3D12CommandQueue>          m_commandQueue;
	ComPtr<ID3D12GraphicsCommandList>   m_commandList;
	ComPtr<ID3D12CommandAllocator>      m_commandAllocators[MAX_FRAME_COUNT];

	// Swap chain objects.
	ComPtr<IDXGIFactory4>               m_dxgiFactory;
	ComPtr<IDXGISwapChain3>             m_swapChain;
	ComPtr<ID3D12Resource>              m_renderTargets[MAX_FRAME_COUNT];
	ComPtr<ID3D12Resource>              m_depthStencil;

	// Presentation fence objects.
	ComPtr<ID3D12Fence>                 m_fence;
	u64									m_fenceValue;
	Microsoft::WRL::Wrappers::Event     m_fenceEvent;

	// Direct3D rendering objects.
	ComPtr<ID3D12DescriptorHeap>        m_rtvDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap>        m_dsvDescriptorHeap;
	u64									m_rtvDescriptorSize;
	D3D12_VIEWPORT                      m_screenViewport;
	D3D12_RECT                          m_scissorRect;

	// Direct3D properties.
	DXGI_FORMAT                         m_backBufferFormat;
	DXGI_FORMAT                         m_depthBufferFormat;
	u32									m_backBufferCount;
	D3D_FEATURE_LEVEL                   m_d3dMinFeatureLevel;

	// Cached device properties.
	HWND                                m_window;
	D3D_FEATURE_LEVEL                   m_d3dFeatureLevel;
	DWORD                               m_dxgiFactoryFlags;
	RECT                                m_outputSize;

	// HDR Support
	DXGI_COLOR_SPACE_TYPE               m_colorSpace;

	u32 m_initFlags;
};