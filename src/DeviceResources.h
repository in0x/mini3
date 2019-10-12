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

static const uint32_t MAX_FRAME_COUNT = 2;

class DeviceResources
{
public:
	enum InitFlags : uint32_t
	{
		IF_EnableDebugLayer = 1 << 0,
		IF_AllowTearing = 1 << 1,
		IF_EnableHDR = 1 << 2
	};

	void Init(HWND window, uint32_t initFlags);
	bool IsTearingAllowed();
	void Flush();

	ID3D12Device*               GetD3DDevice() const { return m_d3dDevice.Get(); }
	IDXGISwapChain3*            GetSwapChain() const { return m_swapChain.Get(); }
	IDXGIFactory4*              GetDXGIFactory() const { return m_dxgiFactory.Get(); }
	D3D_FEATURE_LEVEL           GetDeviceFeatureLevel() const { return m_d3dFeatureLevel; }
	uint64_t					GetCurrentFenceValue() const { return m_fenceValues[m_frameIndex]; }
	ID3D12Resource*             GetCurrentRenderTarget() const { return m_renderTargets[m_frameIndex].Get(); }
	ID3D12Resource*             GetDepthStencil() const { return m_depthStencil.Get(); }
	ID3D12CommandQueue*         GetCommandQueue() const { return m_commandQueue.Get(); }
	ID3D12CommandAllocator*     GetCommandAllocator() const { return m_commandAllocators[m_frameIndex].Get(); }
	ID3D12GraphicsCommandList*  GetCommandList() const { return m_commandList.Get(); }
	ID3D12Fence*				GetFence() const { return m_fence.Get(); }
	HANDLE						GetFenceEvent() { return m_fenceEvent.Get(); }
	DXGI_FORMAT                 GetBackBufferFormat() const { return m_backBufferFormat; }
	DXGI_FORMAT                 GetDepthBufferFormat() const { return m_depthBufferFormat; }
	D3D12_VIEWPORT              GetScreenViewport() const { return m_screenViewport; }
	D3D12_RECT                  GetScissorRect() const { return m_scissorRect; }
	UINT                        GetCurrentFrameIndex() const { return m_frameIndex; }
	UINT                        GetBackBufferCount() const { return m_backBufferCount; }
	DXGI_COLOR_SPACE_TYPE       GetColorSpace() const { return m_colorSpace; }

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView() const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const;

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
	void resizeSwapChain(uint32_t width, uint32_t height, DXGI_FORMAT format);
	void createSwapChain(uint32_t width, uint32_t height, DXGI_FORMAT format);
	void updateColorSpace();
	void createBackBuffers();
	void createDepthBuffer(uint32_t width, uint32_t height);

	uint32_t							m_frameIndex;

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
	uint64_t                            m_fenceValues[MAX_FRAME_COUNT];
	Microsoft::WRL::Wrappers::Event     m_fenceEvent;

	// Direct3D rendering objects.
	ComPtr<ID3D12DescriptorHeap>        m_rtvDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap>        m_dsvDescriptorHeap;
	uint64_t                            m_rtvDescriptorSize;
	D3D12_VIEWPORT                      m_screenViewport;
	D3D12_RECT                          m_scissorRect;

	// Direct3D properties.
	DXGI_FORMAT                         m_backBufferFormat;
	DXGI_FORMAT                         m_depthBufferFormat;
	uint32_t                            m_backBufferCount;
	D3D_FEATURE_LEVEL                   m_d3dMinFeatureLevel;

	// Cached device properties.
	HWND                                m_window;
	D3D_FEATURE_LEVEL                   m_d3dFeatureLevel;
	DWORD                               m_dxgiFactoryFlags;
	RECT                                m_outputSize;

	// HDR Support
	DXGI_COLOR_SPACE_TYPE               m_colorSpace;

	uint32_t m_initFlags;
};