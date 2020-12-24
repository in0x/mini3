#pragma once
#include "Core.h"
#include "Math.h"
#include "GfxTypes.h"

namespace GeoUtils
{
	struct CubeGeometry
	{
		static u32 const num_vertices = 24;
		static u32 const num_indices = 36;
		
		Gfx::Position_t position[num_vertices];
		Gfx::Normal_t   normal[num_vertices];
		Gfx::Tangent_t  tangent_u[num_vertices];
		Gfx::TexCoord_t texcoord[num_vertices];
		
		Gfx::Index_t indices[num_indices];
	};

	static void CreateBox(f32 width, f32 height, f32 depth, CubeGeometry* out_geo)
	{
		MemZeroSafe(out_geo);

		f32 const half_width = width * 0.5f;
		f32 const half_depth = depth * 0.5f;
		f32 const half_height = height * 0.5f;
	
		CubeGeometry& g = *out_geo; // For terser code below.

		// Front	
		g.position[0] = { -half_width, -half_height, -half_depth };
		g.position[1] = { -half_width, +half_height, -half_depth };
		g.position[2] = { +half_width, +half_height, -half_depth };
		g.position[3] = { +half_width, -half_height, -half_depth };
		// Back		  
		g.position[4] = { -half_width, -half_height, +half_depth };
		g.position[5] = { +half_width, -half_height, +half_depth };
		g.position[6] = { +half_width, +half_height, +half_depth };
		g.position[7] = { -half_width, +half_height, +half_depth };
		// Top		  
		g.position[8] = { -half_width, +half_height, -half_depth };
		g.position[9] = { -half_width, +half_height, +half_depth };
		g.position[10] = { +half_width, +half_height, +half_depth };
		g.position[11] = { +half_width, +half_height, -half_depth };
		// Bottom		  
		g.position[12] = { -half_width, -half_height, -half_depth };
		g.position[13] = { +half_width, -half_height, -half_depth };
		g.position[14] = { +half_width, -half_height, +half_depth };
		g.position[15] = { -half_width, -half_height, +half_depth };
		// Left			  
		g.position[16] = { -half_width, -half_height, +half_depth };
		g.position[17] = { -half_width, +half_height, +half_depth };
		g.position[18] = { -half_width, +half_height, -half_depth };
		g.position[19] = { -half_width, -half_height, -half_depth };
		// Right		  
		g.position[20] = { +half_width, -half_height, -half_depth };
		g.position[21] = { +half_width, +half_height, -half_depth };
		g.position[22] = { +half_width, +half_height, +half_depth };
		g.position[23] = { +half_width, -half_height, +half_depth };

		g.normal[0] =  { 0.0f, 0.0f, -1.0f };
		g.normal[1] =  { 0.0f, 0.0f, -1.0f };
		g.normal[2] =  { 0.0f, 0.0f, -1.0f };
		g.normal[3] =  { 0.0f, 0.0f, -1.0f };
		g.normal[4] =  { 0.0f, 0.0f, 1.0f };
		g.normal[5] =  { 0.0f, 0.0f, 1.0f };
		g.normal[6] =  { 0.0f, 0.0f, 1.0f };
		g.normal[7] =  { 0.0f, 0.0f, 1.0f };
		g.normal[8] =  { 0.0f, 1.0f, 0.0f };
		g.normal[9] =  { 0.0f, 1.0f, 0.0f };
		g.normal[10] = { 0.0f, 1.0f, 0.0f };
		g.normal[11] = { 0.0f, 1.0f, 0.0f };
		g.normal[12] = { 0.0f, -1.0f, 0.0f };
		g.normal[13] = { 0.0f, -1.0f, 0.0f };
		g.normal[14] = { 0.0f, -1.0f, 0.0f };
		g.normal[15] = { 0.0f, -1.0f, 0.0f };
		g.normal[16] = { -1.0f, 0.0f, 0.0f };
		g.normal[17] = { -1.0f, 0.0f, 0.0f };
		g.normal[18] = { -1.0f, 0.0f, 0.0f };
		g.normal[19] = { -1.0f, 0.0f, 0.0f };
		g.normal[20] = { 1.0f, 0.0f, 0.0f };
		g.normal[21] = { 1.0f, 0.0f, 0.0f };
		g.normal[22] = { 1.0f, 0.0f, 0.0f };
		g.normal[23] = { 1.0f, 0.0f, 0.0f };

		g.tangent_u[0] = { 1.0f, 0.0f, 0.0f };
		g.tangent_u[1] = { 1.0f, 0.0f, 0.0f };
		g.tangent_u[2] = { 1.0f, 0.0f, 0.0f };
		g.tangent_u[3] = { 1.0f, 0.0f, 0.0f };
		g.tangent_u[4] = { -1.0f, 0.0f, 0.0f };
		g.tangent_u[5] = { -1.0f, 0.0f, 0.0f };
		g.tangent_u[6] = { -1.0f, 0.0f, 0.0f };
		g.tangent_u[7] = { -1.0f, 0.0f, 0.0f };
		g.tangent_u[8] = { 1.0f, 0.0f, 0.0f };
		g.tangent_u[9] = { 1.0f, 0.0f, 0.0f };
		g.tangent_u[10] = { 1.0f, 0.0f, 0.0f };
		g.tangent_u[11] = { 1.0f, 0.0f, 0.0f };
		g.tangent_u[12] = { -1.0f, 0.0f, 0.0f };
		g.tangent_u[13] = { -1.0f, 0.0f, 0.0f };
		g.tangent_u[14] = { -1.0f, 0.0f, 0.0f };
		g.tangent_u[15] = { -1.0f, 0.0f, 0.0f };
		g.tangent_u[16] = { 0.0f, 0.0f, -1.0f };
		g.tangent_u[17] = { 0.0f, 0.0f, -1.0f };
		g.tangent_u[18] = { 0.0f, 0.0f, -1.0f };
		g.tangent_u[19] = { 0.0f, 0.0f, -1.0f };
		g.tangent_u[20] = { 0.0f, 0.0f, 1.0f };
		g.tangent_u[21] = { 0.0f, 0.0f, 1.0f };
		g.tangent_u[22] = { 0.0f, 0.0f, 1.0f };
		g.tangent_u[23] = { 0.0f, 0.0f, 1.0f };

		g.texcoord[0] = { 0.0f, 1.0f };
		g.texcoord[1] = { 0.0f, 0.0f };
		g.texcoord[2] = { 1.0f, 0.0f };
		g.texcoord[3] = { 1.0f, 1.0f };
		g.texcoord[4] = { 1.0f, 1.0f };
		g.texcoord[5] = { 0.0f, 1.0f };
		g.texcoord[6] = { 0.0f, 0.0f };
		g.texcoord[7] = { 1.0f, 0.0f };
		g.texcoord[8] = { 0.0f, 1.0f };
		g.texcoord[9] = { 0.0f, 0.0f };
		g.texcoord[10] = { 1.0f, 0.0f };
		g.texcoord[11] = { 1.0f, 1.0f };
		g.texcoord[12] = { 1.0f, 1.0f };
		g.texcoord[13] = { 0.0f, 1.0f };
		g.texcoord[14] = { 0.0f, 0.0f };
		g.texcoord[15] = { 1.0f, 0.0f };
		g.texcoord[16] = { 0.0f, 1.0f };
		g.texcoord[17] = { 0.0f, 0.0f };
		g.texcoord[18] = { 1.0f, 0.0f };
		g.texcoord[19] = { 1.0f, 1.0f };
		g.texcoord[20] = { 0.0f, 1.0f };
		g.texcoord[21] = { 0.0f, 0.0f };
		g.texcoord[22] = { 1.0f, 0.0f };
		g.texcoord[23] = { 1.0f, 1.0f };

		size_t const num_indices = 36;
		Gfx::Index_t indices[num_indices] = 
		{
			0,  1,  2,	// Front
			0,  2,  3,
			4,  5,  6,	// Back
			4,  6,  7,
			8,  9,  10,	// Top  
			8,  10, 11,
			12, 13, 14, // Bottom
			12, 14, 15,
			16, 17, 18, // Left
			16, 18, 19,
			20, 21, 22, // Right
			20, 22, 23
		};

		memcpy(out_geo->indices, indices, sizeof(Gfx::Index_t) * num_indices);
	}
}