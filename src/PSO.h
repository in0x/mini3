#pragma once

#include "Core.h"
#include "GfxTypes.h"
#include "BasicPSOs.h"

namespace Gfx
{
	struct PSOCache
	{
		void Destroy();

		void CompileBasicPSOs();
		PSO GetBasicPSO(BasicPSO::Enum type);
		GraphicsPSO const* GetPSO(PSO pso_handle);

		static const u32 MAX_PSO = 128;

		Array<PSO, BasicPSO::Count> m_BasicPSOHandles;
		Array<GraphicsPSO, MAX_PSO> m_PSOs;
		Array<Shader, MAX_PSO> m_Shaders;
	};
}
