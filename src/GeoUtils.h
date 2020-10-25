#pragma once
#include "Core.h"
#include "Math.h"

namespace GeoUtils
{
	struct Vertex
	{
		vec3 position;
		vec3 normal;
		vec3 tangent_u;
		vec2 uv;
	};

	typedef u16 Index;

	struct CubeGeometry
	{
		static u32 const num_vertices = 24;
		static u32 const num_indices = 36;

		Vertex vertices[num_vertices];
		Index indices[num_indices];
	};

	static void CreateBox(f32 width, f32 height, f32 depth, CubeGeometry* out_geo)
	{
		f32 const half_width = width * 0.5f;
		f32 const half_depth = depth * 0.5f;
		f32 const half_height = height * 0.5f;
	
		Vertex* vertices = out_geo->vertices;

		// Front
		vertices[0] =  { { -half_width, -half_height, -half_depth }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f } };
		vertices[1] =  { { -half_width, +half_height, -half_depth }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } };
		vertices[2] =  { { +half_width, +half_height, -half_depth }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } };
		vertices[3] =  { { +half_width, -half_height, -half_depth }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f } };
		
		// Back				  
		vertices[4] =  { { -half_width, -half_height, +half_depth }, { 0.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f } };
		vertices[5] =  { { +half_width, -half_height, +half_depth }, { 0.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f } };
		vertices[6] =  { { +half_width, +half_height, +half_depth }, { 0.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } };
		vertices[7] =  { { -half_width, +half_height, +half_depth }, { 0.0f, 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } };
					   	  										  
		// Top				  
		vertices[8] =  { { -half_width, +half_height, -half_depth }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f } };
		vertices[9] =  { { -half_width, +half_height, +half_depth }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } };
		vertices[10] = { { +half_width, +half_height, +half_depth }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } };
		vertices[11] = { { +half_width, +half_height, -half_depth }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f } };
						  										  
		// Bottom				  
		vertices[12] = { { -half_width, -half_height, -half_depth }, { 0.0f, -1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f } };
		vertices[13] = { { +half_width, -half_height, -half_depth }, { 0.0f, -1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f } };
		vertices[14] = { { +half_width, -half_height, +half_depth }, { 0.0f, -1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } };
		vertices[15] = { { -half_width, -half_height, +half_depth }, { 0.0f, -1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } };
						  										  
		// Left					  
		vertices[16] = { { -half_width, -half_height, +half_depth }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f } };
		vertices[17] = { { -half_width, +half_height, +half_depth }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f } };
		vertices[18] = { { -half_width, +half_height, -half_depth }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f } };
		vertices[19] = { { -half_width, -half_height, -half_depth }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f } };
						  										  
		// Right				  
		vertices[20] = { { +half_width, -half_height, -half_depth }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } };
		vertices[21] = { { +half_width, +half_height, -half_depth }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } };
		vertices[22] = { { +half_width, +half_height, +half_depth }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } };
		vertices[23] = { { +half_width, -half_height, +half_depth }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } };

		size_t const num_indices = 36;
		Index indices[num_indices] = 
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

		memcpy(out_geo->indices, indices, sizeof(Index) * num_indices);
	}
}