#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#ifndef NOMINMAX 
#define NOMINMAX 
#endif  

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <wrl/client.h>
#include <wrl/event.h>

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

constexpr size_t MAX_DEBUG_MSG_SIZE = 1024;
char g_debugFmtBuffer[MAX_DEBUG_MSG_SIZE];
char g_debugMsgBuffer[MAX_DEBUG_MSG_SIZE];

int miniFmtDebugMsg(char* buffer, size_t bufferLen, const char *fmt, ...)
{
	va_list ap;
	int retval;

	va_start(ap, fmt);
	retval = sprintf_s(buffer, bufferLen, fmt, ap);
	va_end(ap);

	if (retval < bufferLen)
	{
		buffer[retval + 1] = '\n';
		buffer[retval + 2] = '\0';
	}
	else
	{
		buffer[bufferLen - 2] = '\n';
		buffer[bufferLen - 1] = '\0';
	}

	return retval;
}

#ifdef _DEBUG
#define LOG(format, ...) _snprintf_s(g_debugMsgBuffer, MAX_DEBUG_MSG_SIZE, format, __VA_ARGS__); OutputDebugString(g_debugMsgBuffer); OutputDebugString("\n") 
#else
#define LOG(format, ...)
#endif

#ifdef _DEBUG
#define ASSERT(x) assert(x)
#define ASSERT_F(x, format, ...) if (!(x)) { LOG(format, __VA_ARGS__); assert(x); }
#define ASSERT_RESULT(hr) assert(SUCCEEDED(hr))
#define ASSERT_RESULT_F(hr, format, ...) ASSERT_F(SUCCEEDED(hr), format, __VA_ARGS__)
#else
#define ASSERT(x) 
#define ASSERT_F(x, format, ...)  
#define ASSERT_RESULT(hr) 
#define ASSERT_RESULT_F(hr, format, ...) 
#endif

#define UNUSED(x) (void)(x)

template <class T>
T max(const T& a, const T& b)
{
	return (b > a) ? b : a;
}

template <class T>
T min(const T& a, const T& b)
{
	return (b < a) ? b : a;
}

namespace mini
{
	void LogLastWindowsError()
	{
		LPTSTR errorText = nullptr;
		DWORD lastError = GetLastError();
		DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS;

		FormatMessage(
			flags,
			nullptr, // unused with FORMAT_MESSAGE_FROM_SYSTEM
			lastError,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR)&errorText, 
			0, 
			nullptr);   
		
		if (errorText != nullptr)
		{
			LOG("%s", errorText);
			LocalFree(errorText);
		}
		else
		{
			LOG("Failed to get message for last windows error %u", static_cast<uint32_t>(lastError));
		}
	}

	struct WindowConfig
	{
		uint32_t width;
		uint32_t height;
		uint32_t left;
		uint32_t top;
		const char* title;
		bool bFullscreen;
		bool bAutoShow;
	};

	class WindowClass
	{
		enum { MAX_NAME_LENGTH = 64 };

		char m_className[MAX_NAME_LENGTH];
		WNDPROC m_eventCb;
		
	public:
		WindowClass(const char* className, WNDPROC eventCb)
			: m_eventCb(eventCb)
		{
			strncpy_s(m_className, className, MAX_NAME_LENGTH);

			if (strlen(m_className) >= MAX_NAME_LENGTH)
			{
				m_className[MAX_NAME_LENGTH - 1] = '\n';
			}
		}

		~WindowClass()
		{
			unregisterClass();
		}

		bool registerClass()
		{
			WNDCLASSEX windowClass = {};
			windowClass.cbSize = sizeof(WNDCLASSEX);
			windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
			windowClass.lpfnWndProc = m_eventCb;
			windowClass.hInstance = GetModuleHandle(nullptr);
			windowClass.lpszClassName = m_className;
			
			ATOM classHandle = RegisterClassEx(&windowClass);

			if (classHandle == 0)
			{
				LOG("Failed to register window class type %s!\n", m_className);
				LogLastWindowsError();
			}

			return classHandle != 0;
		}

		void unregisterClass()
		{
			UnregisterClass(m_className, GetModuleHandle(nullptr));
		}

		const char* getClassName() const
		{
			return m_className;
		}
	};

	HWND createWindow(const WindowConfig& config, const WindowClass& windowClass)
	{
		RECT windowRect = {};
		windowRect.left = config.left;
		windowRect.top = config.top;
		windowRect.bottom = config.top + config.height;
		windowRect.right = config.left + config.height;
	
		AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

		DWORD exWindStyle = WS_EX_APPWINDOW;
		DWORD windStyle = 0;

		if (config.bFullscreen)
		{
			DEVMODE displayConfig = {};		
			EnumDisplaySettings(nullptr, ENUM_REGISTRY_SETTINGS, &displayConfig);

			if (ChangeDisplaySettings(&displayConfig, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
			{
				LOG("Failed to fullscreen the window.\n");
				return nullptr;
			}

			windStyle = WS_POPUP;
		}
		else
		{
			exWindStyle |= WS_EX_WINDOWEDGE;
			windStyle = WS_OVERLAPPEDWINDOW;

			// Reconfigures window rect to factor in size of border.
			AdjustWindowRectEx(&windowRect, windStyle, FALSE, exWindStyle);
		}

		HWND window = CreateWindowEx(
			exWindStyle,
			windowClass.getClassName(),
			config.title,
			WS_CLIPSIBLINGS | WS_CLIPCHILDREN | windStyle,
			config.left, config.top,
			config.width, config.height,
			nullptr, // Parent window
			nullptr,
			GetModuleHandle(nullptr),
			nullptr);

		if (window == nullptr)
		{
			LogLastWindowsError();
			return nullptr;
		}

		if (config.bAutoShow)
		{
			ShowWindow(window, SW_SHOW);
		}

		LOG("Created window TITLE: %s WIDTH: %u HEIGHT: %u FULLSCREEN: %d", config.title, config.width, config.height, static_cast<int32_t>(config.bFullscreen));

		return window;
	}

	LRESULT CALLBACK OnMainWindowEvent(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
	{
		// WPARAM -> Word parameter, carries "words" i.e. handle, integers
		// LAPARM -> Long paramter -> carries pointers

		if (message == WM_NCCREATE)
		{
			auto pCreateParams = reinterpret_cast<CREATESTRUCT*>(lParam);
			SetWindowLongPtr(handle, GWLP_USERDATA, reinterpret_cast<uintptr_t>(pCreateParams->lpCreateParams));
		}

		//auto pWindow = reinterpret_cast<Win32Window*>(GetWindowLongPtr(handle, GWLP_USERDATA));

		switch (message)
		{
		case WM_CLOSE:
			PostQuitMessage(0);
			break;

		case WM_SIZE:
			break;

		case WM_KEYDOWN:
			break;

		case WM_KEYUP:
			break;
		}
		return DefWindowProc(handle, message, wParam, lParam);
	}

	using Microsoft::WRL::ComPtr;


	enum InitFlags : uint32_t
	{
		IF_EnableDebugLayer = 1 << 0,
		IF_AllowTearing = 1 << 1,
		IF_EnableHDR = 1 << 2
	};

	class DX12
	{
		static const uint32_t MAX_BACK_BUFFER_COUNT = 3;

		uint32_t							m_backBufferIndex;

		ComPtr<ID3D12Device>                m_d3dDevice;
		ComPtr<ID3D12CommandQueue>          m_commandQueue;
		ComPtr<ID3D12GraphicsCommandList>   m_commandList;
		ComPtr<ID3D12CommandAllocator>      m_commandAllocators[MAX_BACK_BUFFER_COUNT];

		// Swap chain objects.
		ComPtr<IDXGIFactory4>               m_dxgiFactory;
		ComPtr<IDXGISwapChain3>             m_swapChain;
		ComPtr<ID3D12Resource>              m_renderTargets[MAX_BACK_BUFFER_COUNT];
		ComPtr<ID3D12Resource>              m_depthStencil;

		// Presentation fence objects.
		ComPtr<ID3D12Fence>                 m_fence;
		uint64_t                            m_fenceValues[MAX_BACK_BUFFER_COUNT];
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

		void enableDebugLayer();
		bool checkTearingSupport();
		void createDevice(bool bEnableDebugLayer);
		void checkFeatureLevel();
		void createCommandQueue();
		void createDescriptorHeaps();
		void createCommandAllocators();
		void createCommandList();
		void createEndOfFrameFence();
		
		void WaitForGpu();
		void initWindowSizeDependent();
		void resizeSwapChain(uint32_t width, uint32_t height, DXGI_FORMAT format);
		void createSwapChain(uint32_t width, uint32_t height, DXGI_FORMAT format);
		void updateColorSpace();
		void createBackBuffers();
		void createDepthBuffer(uint32_t width, uint32_t height);

	public:
		void init(HWND window, uint32_t initFlags);
	};

	// This method acquires the first available hardware adapter that supports Direct3D 12.
	// If no such adapter can be found, try WARP. Otherwise throw an exception.
	IDXGIAdapter1* getFirstAvailableHardwareAdapter(ComPtr<IDXGIFactory4> dxgiFactory, D3D_FEATURE_LEVEL minFeatureLevel)
	{
		ComPtr<IDXGIAdapter1> adapter;

		uint32_t adapterIndex = 0;
		HRESULT getAdapterResult = S_OK;

		while (getAdapterResult != DXGI_ERROR_NOT_FOUND)
		{
			getAdapterResult = dxgiFactory->EnumAdapters1(adapterIndex, adapter.ReleaseAndGetAddressOf());
		

			DXGI_ADAPTER_DESC1 desc;
			ASSERT_RESULT(adapter->GetDesc1(&desc));

			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				// Don't select the Basic Render Driver adapter.
				continue;
			}

			// Check to see if the adapter supports Direct3D 12, but don't create the actual device yet.
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), minFeatureLevel, _uuidof(ID3D12Device), nullptr)))
			{
				LOG("Direct3D Adapter (%u): VID:%04X, PID:%04X - %ls\n", adapterIndex, desc.VendorId, desc.DeviceId, desc.Description);
				break;
			}

			adapterIndex++;
		}

		if (!adapter)
		{
			// Try WARP12 instead
			if (FAILED(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()))))
			{
				ASSERT_F(false, "WARP12 not available. Enable the 'Graphics Tools' optional feature");
			}

			LOG("Direct3D Adapter - WARP12\n");
		}

		ASSERT_F(adapter != nullptr, "No Direct3D 12 device found");
		return adapter.Detach();
	}

	void DX12::init(HWND window, uint32_t initFlags)
	{
		const bool bEnableDebugLayer = initFlags & IF_EnableDebugLayer;
		const bool bWantAllowTearing = initFlags & IF_AllowTearing;
		bool bAllowTearing = bWantAllowTearing;

		m_window = window;

		m_d3dMinFeatureLevel = D3D_FEATURE_LEVEL_11_0;
		m_d3dFeatureLevel = D3D_FEATURE_LEVEL_11_0;

		m_backBufferIndex = 0;
		m_backBufferCount = 2;

		m_rtvDescriptorSize = 0;
		m_backBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
		m_depthBufferFormat = DXGI_FORMAT_D32_FLOAT;

		m_dxgiFactoryFlags = 0;
		m_outputSize = { 0, 0, 1, 1 };
		m_colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
			
		if (bEnableDebugLayer)
		{
			enableDebugLayer();
		}

		HRESULT createFactoryResult = CreateDXGIFactory2(m_dxgiFactoryFlags, IID_PPV_ARGS(m_dxgiFactory.ReleaseAndGetAddressOf()));
		ASSERT_RESULT(createFactoryResult);
	
		if (bWantAllowTearing)
		{
			bAllowTearing = checkTearingSupport();

			if (!bAllowTearing)
			{
				initFlags &= ~IF_AllowTearing;
			}
		}

		m_initFlags = initFlags;

		createDevice(bEnableDebugLayer);
		checkFeatureLevel();
		createCommandQueue();
		createDescriptorHeaps();
		createCommandAllocators();
		createCommandList();
		createEndOfFrameFence();

		initWindowSizeDependent();
	}

	DXGI_FORMAT formatSrgbToLinear(DXGI_FORMAT fmt)
	{
		switch (fmt)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:   return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:   return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:   return DXGI_FORMAT_B8G8R8X8_UNORM;
		default:                                return fmt;
		}
	}

	void DX12::resizeSwapChain(uint32_t width, uint32_t height, DXGI_FORMAT format)
	{
		HRESULT hr = m_swapChain->ResizeBuffers(
			m_backBufferCount,
			width,
			height,
			format,
			(m_initFlags & IF_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0
		);

		if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
		{
			char err[64] = {};
			sprintf_s(err, "Device Lost on ResizeBuffers: Reason code 0x%08X\n", (hr == DXGI_ERROR_DEVICE_REMOVED) ? m_d3dDevice->GetDeviceRemovedReason() : hr);
			
			ASSERT_F(false, err);
		}
	}
	
	void DX12::createSwapChain(uint32_t width, uint32_t height, DXGI_FORMAT format)
	{

		// Create a descriptor for the swap chain.
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = width;
		swapChainDesc.Height = height;
		swapChainDesc.Format = format;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = m_backBufferCount;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		swapChainDesc.Flags = (m_initFlags & IF_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
		fsSwapChainDesc.Windowed = TRUE;

		ComPtr<IDXGISwapChain1> swapChain;

		HRESULT hr = m_dxgiFactory->CreateSwapChainForHwnd(
			m_commandQueue.Get(),
			m_window,
			&swapChainDesc,
			&fsSwapChainDesc,
			nullptr,
			swapChain.GetAddressOf()
		);

		ASSERT_RESULT(hr);

		hr = swapChain.As(&m_swapChain);
		ASSERT_RESULT(hr);

		// This class does not support exclusive full-screen mode and prevents DXGI from responding to the ALT+ENTER shortcut
		hr = m_dxgiFactory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER);
		ASSERT_RESULT(hr);
	}

	void DX12::updateColorSpace()
	{
		DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
		bool bIsDisplayHDR10 = false;
		HRESULT hr = S_OK;

#if USES_DXGI6
		ASSERT(m_swapChain);
		
		ComPtr<IDXGIOutput> output;
		hr = m_swapChain->GetContainingOutput(output.GetAddressOf());
		ASSERT_RESULT(hr);

		ComPtr<IDXGIOutput6> output6;
		ASSERT_RESULT(output.As(&output6));

		DXGI_OUTPUT_DESC1 desc;
		ASSERT_RESULT(output6->GetDesc1(&desc));

		bIsDisplayHDR10 = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
#endif

		if ((m_initFlags & InitFlags::IF_EnableHDR) && bIsDisplayHDR10)
		{
			switch (m_backBufferFormat)
			{
			case DXGI_FORMAT_R10G10B10A2_UNORM:
			{
				// The application creates the HDR10 signal.
				colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
				break;
			}
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
			{
				// The system creates the HDR10 signal; application uses linear values.
				colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
				break;
			}
			default:
			{
				// Not sure if this is a valid case.
				ASSERT(false);
				break;
			}
			}
		}

		m_colorSpace = colorSpace;

		uint32_t colorSpaceSupport = 0;
		hr = m_swapChain->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport);
		
		if (SUCCEEDED(hr) && (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
		{
			hr = m_swapChain->SetColorSpace1(colorSpace);
			ASSERT_RESULT(hr);
		}

	}
	
	void DX12::initWindowSizeDependent()
	{
		ASSERT(m_window);

		WaitForGpu();

		// Release resources that are tied to the swap chain and update fence values.
		for (uint32_t n = 0; n < m_backBufferCount; n++)
		{
			m_renderTargets[n].Reset();
			m_fenceValues[n] = m_fenceValues[m_backBufferIndex];
		}

		const uint32_t backBufferWidth = max(static_cast<uint32_t>(m_outputSize.right - m_outputSize.left), 1u);
		const uint32_t backBufferHeight = max(static_cast<uint32_t>(m_outputSize.bottom - m_outputSize.top), 1u);
		const DXGI_FORMAT backBufferFormat = formatSrgbToLinear(m_backBufferFormat);

		// If the swap chain already exists, resize it, otherwise create one.
		if (m_swapChain)
		{
			resizeSwapChain(backBufferWidth, backBufferHeight, backBufferFormat);
		}
		else
		{
			createSwapChain(backBufferWidth, backBufferHeight, backBufferFormat);
		}

		updateColorSpace();

		createBackBuffers();
	
		// Reset the index to the current back buffer.
		m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

		if (m_depthBufferFormat != DXGI_FORMAT_UNKNOWN)
		{
			createDepthBuffer(backBufferWidth, backBufferHeight);
		}

		// Set the 3D rendering viewport and scissor rectangle to target the entire window.
		m_screenViewport.TopLeftX = m_screenViewport.TopLeftY = 0.f;
		m_screenViewport.Width = static_cast<float>(backBufferWidth);
		m_screenViewport.Height = static_cast<float>(backBufferHeight);
		m_screenViewport.MinDepth = D3D12_MIN_DEPTH;
		m_screenViewport.MaxDepth = D3D12_MAX_DEPTH;

		m_scissorRect.left = m_scissorRect.top = 0;
		m_scissorRect.right = backBufferWidth;
		m_scissorRect.bottom = backBufferHeight;
	}

	void DX12::createBackBuffers()
	{
		for (int32_t i = 0; i < m_backBufferCount; i++)
		{
			HRESULT hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(m_renderTargets[i].GetAddressOf()));
			ASSERT_RESULT(hr);

			wchar_t name[25] = {};
			swprintf_s(name, L"Render target %d", i);
			m_renderTargets[i]->SetName(name);

			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
			rtvDesc.Format = m_backBufferFormat;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

			CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptor(m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), i, m_rtvDescriptorSize);
			m_d3dDevice->CreateRenderTargetView(m_renderTargets[i].Get(), &rtvDesc, rtvDescriptor);
		}
	}

	void DX12::createDepthBuffer(uint32_t width, uint32_t height)
	{

		// Allocate a 2-D surface as the depth/stencil buffer and create a depth/stencil view
		// on this surface.
		CD3DX12_HEAP_PROPERTIES depthHeapProperties(D3D12_HEAP_TYPE_DEFAULT);

		D3D12_RESOURCE_DESC depthStencilDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			m_depthBufferFormat,
			width,
			height,
			1, // This depth stencil view has only one texture.
			1  // Use a single mipmap level.
		);
		depthStencilDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
		depthOptimizedClearValue.Format = m_depthBufferFormat;
		depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
		depthOptimizedClearValue.DepthStencil.Stencil = 0;

		HRESULT hr = m_d3dDevice->CreateCommittedResource(
			&depthHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&depthStencilDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(m_depthStencil.ReleaseAndGetAddressOf())
		);
		
		ASSERT_RESULT(hr);

		m_depthStencil->SetName(L"Depth stencil");

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = m_depthBufferFormat;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

		m_d3dDevice->CreateDepthStencilView(m_depthStencil.Get(), &dsvDesc, m_dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart()); 
	}

	void DX12::WaitForGpu() 
	{
		if (!m_commandQueue || !m_fence || !m_fenceEvent.IsValid())
		{
			return;
		}
		
		// Schedule a Signal command in the GPU queue.
		uint64_t fenceValue = m_fenceValues[m_backBufferIndex];
		
		if (!SUCCEEDED(m_commandQueue->Signal(m_fence.Get(), fenceValue)))
		{
			return;
		}

		// Wait until the Signal has been processed.
		if (!SUCCEEDED(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent.Get())))
		{
			return;
		}
		
		WaitForSingleObjectEx(m_fenceEvent.Get(), INFINITE, FALSE);

		// Increment the fence value for the current frame.
		m_fenceValues[m_backBufferIndex]++;
	}

	void DX12::enableDebugLayer() 
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf()))))
		{
			debugController->EnableDebugLayer();
		}
		else
		{
			LOG("WARNING: Direct3D Debug Device is not available\n");
		}

		ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(dxgiInfoQueue.GetAddressOf()))))
		{
			m_dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;

			dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
			dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
		}
	}

	bool DX12::checkTearingSupport()
	{
		BOOL allowTearing = FALSE;
		ComPtr<IDXGIFactory5> factory5;
		HRESULT hr = m_dxgiFactory.As(&factory5);

		if (SUCCEEDED(hr))
		{
			hr = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
		}

		bool bAllowTearing = SUCCEEDED(hr) && allowTearing;

		if (!bAllowTearing)
		{
			LOG("Variable refresh rate displays not supported");
		}
		
		return bAllowTearing;
	}

	void DX12::createDevice(bool bEnableDebugLayer)
	{
		ComPtr<IDXGIAdapter1> adapter;
		*adapter.GetAddressOf() = getFirstAvailableHardwareAdapter(m_dxgiFactory, m_d3dMinFeatureLevel);

		D3D12CreateDevice(
			adapter.Get(),
			m_d3dMinFeatureLevel,
			IID_PPV_ARGS(m_d3dDevice.ReleaseAndGetAddressOf())
		);

		m_d3dDevice->SetName(L"DeviceResources");

		// Configure debug device (if active).
		ComPtr<ID3D12InfoQueue> d3dInfoQueue;
		if (SUCCEEDED(m_d3dDevice.As(&d3dInfoQueue)))
		{
			if (bEnableDebugLayer)
			{
				d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
				d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
			}

			D3D12_MESSAGE_ID hide[] =
			{
				D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
				D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE
			};

			D3D12_INFO_QUEUE_FILTER filter = {};
			filter.DenyList.NumIDs = _countof(hide);
			filter.DenyList.pIDList = hide;
			d3dInfoQueue->AddStorageFilterEntries(&filter);
		}
	}

	void DX12::checkFeatureLevel()
	{
		// Determine maximum supported feature level for this device
		static const D3D_FEATURE_LEVEL s_featureLevels[] =
		{
			D3D_FEATURE_LEVEL_12_1,
			D3D_FEATURE_LEVEL_12_0,
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};

		D3D12_FEATURE_DATA_FEATURE_LEVELS featLevels =
		{
			_countof(s_featureLevels), s_featureLevels, D3D_FEATURE_LEVEL_11_0
		};

		HRESULT hr = m_d3dDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featLevels, sizeof(featLevels));
		if (SUCCEEDED(hr))
		{
			m_d3dFeatureLevel = featLevels.MaxSupportedFeatureLevel;
		}
		else
		{
			m_d3dFeatureLevel = m_d3dMinFeatureLevel;
		}
	}

	void DX12::createCommandQueue()
	{
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

		HRESULT queueCreated = m_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(m_commandQueue.ReleaseAndGetAddressOf()));
		ASSERT_RESULT(queueCreated);

		m_commandQueue->SetName(L"DeviceResources");
	}

	void DX12::createDescriptorHeaps()
	{
		// Render targets
		D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {};
		rtvDescriptorHeapDesc.NumDescriptors = m_backBufferCount;
		rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

		HRESULT descrHeapCreated = m_d3dDevice->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(m_rtvDescriptorHeap.ReleaseAndGetAddressOf()));
		ASSERT_RESULT(descrHeapCreated);

		m_rtvDescriptorHeap->SetName(L"DeviceResources");
		m_rtvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// Depth Stencil Views
		ASSERT(m_depthBufferFormat != DXGI_FORMAT_UNKNOWN);

		D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc = {};
		dsvDescriptorHeapDesc.NumDescriptors = 1;
		dsvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

		descrHeapCreated = m_d3dDevice->CreateDescriptorHeap(&dsvDescriptorHeapDesc, IID_PPV_ARGS(m_dsvDescriptorHeap.ReleaseAndGetAddressOf()));

		m_dsvDescriptorHeap->SetName(L"DeviceResources");
	}

	void DX12::createCommandAllocators()
	{
		for (uint32_t n = 0; n < m_backBufferCount; n++)
		{
			HRESULT createdAllocator = m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_commandAllocators[n].ReleaseAndGetAddressOf()));
			ASSERT_RESULT(createdAllocator);

			wchar_t name[25] = {};
			swprintf_s(name, L"Render target %u", n);
			m_commandAllocators[n]->SetName(name);
		}
	}

	void DX12::createCommandList()
	{
		HRESULT cmdListCreated = m_d3dDevice->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			m_commandAllocators[0].Get(),
			nullptr,
			IID_PPV_ARGS(m_commandList.ReleaseAndGetAddressOf()));

		ASSERT_RESULT(cmdListCreated);
		ASSERT_RESULT(m_commandList->Close());

		m_commandList->SetName(L"DeviceResources");
	}

	void DX12::createEndOfFrameFence()
	{
		// Create a fence for tracking GPU execution progress.
		HRESULT createdFence = m_d3dDevice->CreateFence(m_fenceValues[m_backBufferIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.ReleaseAndGetAddressOf()));
		ASSERT_RESULT(createdFence);
		m_fenceValues[m_backBufferIndex]++;

		m_fence->SetName(L"DeviceResources");

		m_fenceEvent.Attach(CreateEvent(nullptr, FALSE, FALSE, nullptr));
		ASSERT(m_fenceEvent.IsValid());
	}
}

INT WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, INT nCmdShow)
{
	UNUSED(hInstance);
	UNUSED(hPrevInstance);
	UNUSED(lpCmdLine);
	UNUSED(nCmdShow);

	using namespace mini;

	LOG("Initializing mini3");

	WindowClass mainWindowClass("mini3::Window", &mini::OnMainWindowEvent);

	if (!mainWindowClass.registerClass())
	{
		LOG("Failed to register main window class!");
		return -1;
	}

	WindowConfig config;
	config.width = 1200;
	config.height = 700;
	config.left = 0;
	config.top = 0;
	config.title = "mini3";
	config.bFullscreen = false;
	config.bAutoShow = true;

	HWND window = createWindow(config, mainWindowClass);

	if (window == nullptr)
	{
		LOG("Failed to create main window!");
		return -1;
	}

	//SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(g_game.get()));
	//GetClientRect(hwnd, &rc);

	DX12 resources;

	resources.init(window, InitFlags::IF_EnableDebugLayer | InitFlags::IF_AllowTearing);
	
	MSG msg = {};
	while (WM_QUIT != msg.message)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
		}
	}


	return 0;
}