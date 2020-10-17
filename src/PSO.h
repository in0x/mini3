#pragma once

#include "Core.h"
#include "GpuDeviceDX12.h"
#include "d3dcompiler.h"

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
		}
	};

	static ComPtr<ID3DBlob> CompileShader(wchar_t const* filename, ShaderCompilationSettings const* settings)
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

		ASSERT_HR_F(result, (char*)errors->GetBufferPointer());

		if (SUCCEEDED(result))
		{
			return byte_code;
		}
		else
		{
			return nullptr;
		}
	}

	static Shader CreateShader(wchar_t const* filename, ShaderStage::Enum stage)
	{
		Shader shader;
		shader.blob = nullptr;

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
		}
		case ShaderStage::Pixel:
		{
			D3D_SHADER_MACRO macros[2] = { "PIXEL_SHADER" , "1" };
			settings.macros = macros;
			settings.entry_point = "ps_main";
			settings.target = "ps_5_1";
			shader.blob = CompileShader(filename, &settings);
		}
		case ShaderStage::Compute:
		{
			D3D_SHADER_MACRO macros[2] = { "COMPUTE_SHADER" , "1" };
			settings.macros = macros;
			settings.entry_point = "cs_main";
			settings.target = "cs_5_1";
			shader.blob = CompileShader(filename, &settings);

		}
		default:
		{
			ASSERT_FAIL("Tried to compile shader of unsupported stage!");
		}
		}

		return shader;
	}

	static void CreateSimpleColorPSOs()
	{
		Shader vert_shader = CreateShader(L"Shaders\\VertexColor.hlsl", ShaderStage::Vertex);
		Shader pixl_shader = CreateShader(L"Shaders\\VertexColor.hlsl", ShaderStage::Pixel);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;

		pso_desc.VS.pShaderBytecode = vert_shader.blob->GetBufferPointer();
		pso_desc.VS.BytecodeLength =  vert_shader.blob->GetBufferSize();
		
		pso_desc.PS.pShaderBytecode = pixl_shader.blob->GetBufferPointer();
		pso_desc.PS.BytecodeLength =  pixl_shader.blob->GetBufferSize();



	}
}
