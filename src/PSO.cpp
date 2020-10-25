#include "PSO.h"
#include "d3dcompiler.h"
#include "GpuDeviceDX12.h"

namespace Gfx
{
	struct ShaderCompilationSettings
	{
		D3D_SHADER_MACRO const* macros;
		char const*				entry_point;
		char const*				target;
		bool					compile_debug;
		bool					skip_optimization;

		u32 GetFlags() const
		{
			u32 flags = 0;

			if (compile_debug)
			{
				flags |= D3DCOMPILE_DEBUG;
			}

			if (skip_optimization)
			{
				flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
			}

			return flags;
		}
	};

	static ID3DBlob* CompileShader(wchar_t const* filename, ShaderCompilationSettings const* settings)
	{
		u32 flags = settings->GetFlags();

		ComPtr<ID3DBlob> byte_code = nullptr;
		ComPtr<ID3DBlob> errors = nullptr;

		HRESULT result = D3DCompileFromFile(
			filename,
			settings->macros,
			D3D_COMPILE_STANDARD_FILE_INCLUDE,
			settings->entry_point,
			settings->target,
			flags,
			0,
			&byte_code,
			&errors);

		if (SUCCEEDED(result))
		{
			return byte_code.Detach();
		}
		else
		{
			if (errors)
			{
				ASSERT_HR_F(result, "%s", (char*)errors->GetBufferPointer());
			}
			else
			{
				ASSERT_HR_F(result, "Failed shader compilation, check HR!");
			}

			return nullptr;
		}
	}

	static Shader CreateShader(wchar_t const* filename, ShaderStage::Enum stage)
	{
		Shader shader;
		shader.blob = nullptr;
		shader.stage = ShaderStage::Count;

		ShaderCompilationSettings settings;
		MemZeroSafe(settings);

		// Since this is a content setting, we'd want smarter options for this later.
		// But for now this will do.
#ifdef _DEBUG
		settings.compile_debug = true;
		settings.skip_optimization = true;
#else
		settings.compile_debug = false;
		settings.skip_optimization = false;
#endif

		switch (stage)
		{
		case ShaderStage::Vertex:
		{
			D3D_SHADER_MACRO macros[2] = { "VERTEX_SHADER" , "1" };
			settings.macros = macros;
			settings.entry_point = "vs_main";
			settings.target = "vs_5_1";
			shader.blob = CompileShader(filename, &settings);
			shader.stage = ShaderStage::Vertex;
			break;
		}
		case ShaderStage::Pixel:
		{
			D3D_SHADER_MACRO macros[2] = { "PIXEL_SHADER" , "1" };
			settings.macros = macros;
			settings.entry_point = "ps_main";
			settings.target = "ps_5_1";
			shader.blob = CompileShader(filename, &settings);
			shader.stage = ShaderStage::Pixel;
			break;
		}
		case ShaderStage::Compute:
		{
			D3D_SHADER_MACRO macros[2] = { "COMPUTE_SHADER" , "1" };
			settings.macros = macros;
			settings.entry_point = "cs_main";
			settings.target = "cs_5_1";
			shader.blob = CompileShader(filename, &settings);
			shader.stage = ShaderStage::Compute;
			break;
		}
		default:
		{
			ASSERT_FAIL_F("Tried to compile shader of unsupported stage!");
			break;
		}
		}

		ASSERT(shader.blob);
		return shader;
	}

	static void DefaultInitGraphicsPsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc)
	{
		MemZeroSafe(desc);
		desc->RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		desc->RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		desc->RasterizerState.DepthClipEnable = true;
		desc->RasterizerState.FrontCounterClockwise = false;
		desc->RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		u32 const numRtBlends = ARRAY_SIZE(desc->BlendState.RenderTarget);
		for (u32 i = 0; i < numRtBlends; ++i)
		{
			desc->BlendState.RenderTarget[i].BlendEnable = false;
			desc->BlendState.RenderTarget[i].SrcBlend = D3D12_BLEND_SRC_ALPHA;
			desc->BlendState.RenderTarget[i].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			desc->BlendState.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
			desc->BlendState.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
			desc->BlendState.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ONE;
			desc->BlendState.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
			desc->BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		}

		desc->SampleDesc.Count = 1;
		desc->SampleDesc.Quality = 0;
		desc->SampleMask = 0xFFFFFFFF;
		desc->DSVFormat = DXGI_FORMAT_UNKNOWN;
		desc->IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
		desc->PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	}

	void PSOCache::Destroy()
	{
		for (GraphicsPSO const& pso : m_PSOs)
		{
			pso.pso->Release();
		}

		for (Shader const& shader : m_Shaders)
		{
			shader.blob->Release();
		}
	}

	void PSOCache::CompileBasicPSOs()
	{
		Shader* vert_shader = m_Shaders.PushBack(CreateShader(L"src\\shaders\\VertexColor.hlsl", ShaderStage::Vertex));
		Shader* pixl_shader = m_Shaders.PushBack(CreateShader(L"src\\shaders\\VertexColor.hlsl", ShaderStage::Pixel));

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
		DefaultInitGraphicsPsoDesc(&pso_desc);

		pso_desc.VS.pShaderBytecode = vert_shader->blob->GetBufferPointer();
		pso_desc.VS.BytecodeLength = vert_shader->blob->GetBufferSize();

		pso_desc.PS.pShaderBytecode = pixl_shader->blob->GetBufferPointer();
		pso_desc.PS.BytecodeLength = pixl_shader->blob->GetBufferSize();

		D3D12_INPUT_ELEMENT_DESC elements[4];

		pso_desc.InputLayout;
		pso_desc.InputLayout.NumElements = 4;
		pso_desc.InputLayout.pInputElementDescs = elements;

		elements[0].SemanticName = "POSITION";
		elements[0].SemanticIndex = 0;
		elements[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
		elements[0].InputSlot = 0;
		elements[0].AlignedByteOffset = 0;
		elements[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		elements[0].InstanceDataStepRate = 0;

		elements[1].SemanticName = "NORMAL";
		elements[1].SemanticIndex = 0;
		elements[1].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		elements[1].InputSlot = 0;
		elements[1].AlignedByteOffset = 12;
		elements[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		elements[1].InstanceDataStepRate = 0;

		elements[2].SemanticName = "TANGENT";
		elements[2].SemanticIndex = 0;
		elements[2].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		elements[2].InputSlot = 0;
		elements[2].AlignedByteOffset = 24;
		elements[2].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		elements[2].InstanceDataStepRate = 0;

		elements[3].SemanticName = "TEXCOORD";
		elements[3].SemanticIndex = 0;
		elements[3].Format = DXGI_FORMAT_R32G32_FLOAT;
		elements[3].InputSlot = 0;
		elements[3].AlignedByteOffset = 36;
		elements[3].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		elements[3].InstanceDataStepRate = 0;

		pso_desc.DepthStencilState.DepthEnable = true;
		pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		pso_desc.NumRenderTargets = 1;
		pso_desc.RTVFormats[0] = GetBackBufferFormat();
		pso_desc.DSVFormat = GetDSFormat();

		GraphicsPSO vert_color_solid = CreateGraphicsPSO(&pso_desc);

		pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		GraphicsPSO vert_color_wire = CreateGraphicsPSO(&pso_desc);

		if (vert_color_solid.pso != nullptr)
		{
			u32 handle_solid = m_PSOs.Size();
			m_PSOs.PushBack(vert_color_solid);

			ASSERT(m_BasicPSOHandles.Size() == BasicPSO::VertexColorSolid);
			Gfx::PSO* pso = m_BasicPSOHandles.PushBack();
			pso->handle = handle_solid;
		}
		else
		{
			ASSERT_FAIL_F("Failed to compile pso vert_color_solid!");
		}

		if (vert_color_wire.pso != nullptr)
		{
			u32 handle_wire = m_PSOs.Size();
			m_PSOs.PushBack(vert_color_wire);

			ASSERT(m_BasicPSOHandles.Size() == BasicPSO::VertexColorWireframe);
			Gfx::PSO* pso = m_BasicPSOHandles.PushBack();
			pso->handle = handle_wire;
		}
		else
		{
			ASSERT_FAIL_F("Failed to compile pso vert_color_wire!");
		}
	}

	PSO PSOCache::GetBasicPSO(BasicPSO::Enum type)
	{
		return m_BasicPSOHandles[type];
	}

	GraphicsPSO const* PSOCache::GetPSO(PSO pso_handle)
	{
		return &m_PSOs[pso_handle.handle];
	}
}