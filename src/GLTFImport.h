#pragma once
#include "Core.h"
#include "Memory.h"

#include <io.h>

#define CGLTF_IMPLEMENTATION
#include "external/cgltf/cgltf.h"

/*
GLTF Notes:
	-) Right handed coordinates:. Front of asset faces +z.
		+y
		|__+x
	 +z/

	 -) Distances in m. Angles in rad. Positive rotation is CCW

	 -) If vertex data is interleaved, accessor has a non-zero byte
	    offset which locates the first relevant element in the buffer.
		The bufferview will have a non-zero stride, which defines the
		element step through the buffer.
*/

namespace Mini
{
	struct SceneImporter
	{
		char const* file_path;
		Memory::Arena* scratch_memory;
		Memory::Arena* mesh_memory;
	};

	static u8* ReadFileBuffer(char const* file_path, Memory::Arena* buffer_allocator, u64* bytes_read)
	{
		*bytes_read = 0;

		HANDLE fstream = CreateFile(file_path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (fstream == INVALID_HANDLE_VALUE)
		{
			ASSERT("Failed to open file %s!");
			return nullptr;
		}

		ON_SCOPE_EXIT(CloseHandle(fstream));

		LARGE_INTEGER file_size_out;
		if (!GetFileSizeEx(fstream, &file_size_out))
		{
			LogLastWindowsError();
			ASSERT("Failed to get file size!");
			return nullptr;
		}

		u64 stream_len = file_size_out.QuadPart;
		if (stream_len == 0)
		{
			ASSERT("File stream is empty!");
			return nullptr;
		}

		u8* file_data = (u8*)Memory::PushSize(buffer_allocator, stream_len, Memory::ZeroPush());

		DWORD num_bytes_read = 0;
		if (!ReadFile(fstream, file_data, stream_len, &num_bytes_read, nullptr))
		{
			LogLastWindowsError();
			ASSERT_FAIL_F("Failed to read file from stream!");
			return nullptr;
		}

		if (num_bytes_read != stream_len)
		{
			ASSERT_FAIL_F("Did not read the expected number of bytes from stream!");
			return nullptr;
		}

		*bytes_read = stream_len;
		return file_data;
	}

	static u64 CalculateTotalBufferSize(cgltf_accessor* accessor)
	{
		u16 num_components = 0;
		switch (accessor->type)
		{
		case cgltf_type_vec2:
			num_components = 2;
			break;
		case cgltf_type_vec3:
			num_components = 3;
			break;
		case cgltf_type_vec4:
		case cgltf_type_mat2:
			num_components = 4;
			break;
		case cgltf_type_mat3:
			num_components = 9;
			break;
		case cgltf_type_mat4:
			num_components = 16;
			break;
		case cgltf_type_invalid:
		default:
			ASSERT_FAIL();
		case cgltf_type_scalar:
			num_components = 1;
			break;
		}

		u32 component_size = 0;
		switch (accessor->component_type)
		{
		case cgltf_component_type_r_8:
		case cgltf_component_type_r_8u:
			component_size = 1;
			break;
		case cgltf_component_type_r_16:
		case cgltf_component_type_r_16u:
			component_size = 2;
			break;
		case cgltf_component_type_r_32u:
		case cgltf_component_type_r_32f:
			component_size = 4;
			break;
		case cgltf_component_type_invalid:
		default:
			ASSERT_FAIL();
			break;
		}

		u64 total_buffer_size = num_components * component_size * accessor->count;
		return total_buffer_size;
	}

	
	static void CopyBuffer(u8* dst, u64 dst_size, cgltf_type dst_type, cgltf_component_type dst_component, cgltf_accessor* accessor)
	{
		if (dst_type != accessor->type)
		{
			ASSERT_FAIL_F("Doesnt make sense to copy different types (e.g. scalar->vec3)!");
			return;
		}

		if (dst_component != accessor->component_type)
		{
			// NOTE(): Easy to implement, just dont need to atm.
			ASSERT_FAIL_F("Mismatching component type copy not implemented atm!");
		}

		u64 src_size = accessor->buffer_view->size;
		ASSERT(src_size == CalculateTotalBufferSize(accessor));
		if (dst_size < src_size)
		{
			ASSERT_FAIL_F("Did not pre-allocate enough space for buffer!");
			return;
		}

		u8* src = (u8*)accessor->buffer_view->buffer->data;
		src += accessor->buffer_view->offset;

		memcpy_s(dst, dst_size, src, src_size);
	}

	struct MeshImport
	{
		u16* index_buffer;
		u32 num_indices;

		u32 num_vertices;

		vec3* position_buffer;
		vec3* normal_buffer;
		vec2* texcoord_buffer;
	};

	static void FlipTriangleWinding(Gfx::Index_t* vertices, u64 const count)
	{
		ASSERT(count % 3 == 0);

		u64 last_tri = count - 3;
		for (u64 i = 0; i <= last_tri; i += 3)
		{
			Gfx::Index_t temp = vertices[i + 1];
			vertices[i + 1] = vertices[i + 2];
			vertices[i + 2] = temp;
		}
	}

	static void InvertZ(vec3* vertices, u64 const count)
	{
		for (u64 i = 0; i < count; ++i)
		{
			vertices[i].z = -vertices[i].z;
		}
	}

	static void ChangeBasis(vec3* vertices, u64 const count)
	{
		mat44 basis_change = mat44::Identity();
		basis_change(2, 2) = -1.0f;

		for (u64 i = 0; i < count; ++i)
		{
			vec4 v = vertices[i];
			v.w = 1.0f;
			vertices[i] = Math::Mul(basis_change, v).xyz;
		}
	}

	static MeshImport Import(SceneImporter* importer)
	{
		Memory::TemporaryAllocation alloc = BeginTemporaryAlloc(importer->scratch_memory);
		ON_SCOPE_EXIT(Memory::RewindTemporaryAlloc(importer->scratch_memory, alloc, false));
		
		MeshImport imported;
		MemZeroSafe(imported);

		u64 stream_len;
		u8* file_data = ReadFileBuffer(importer->file_path, importer->scratch_memory, &stream_len);
		if (file_data == nullptr)
		{
			return imported;
		}

		cgltf_options options;
		MemZeroSafe(options);

		struct Local
		{
			static void* AllocFromArena(void* user, u64 size)
			{
				Memory::Arena* arena = static_cast<Memory::Arena*>(user);
				return Memory::PushSize(arena, size);
			}

			static void FreeFromArena(void* user, void* data) 
			{ 
				/* NO-OP */ 
			};
		};

		options.memory.alloc = &Local::AllocFromArena;
		options.memory.free = &Local::FreeFromArena;
		options.memory.user_data = importer->scratch_memory;

		cgltf_data* scene_data;
		cgltf_result result = cgltf_parse(&options, file_data, stream_len, &scene_data);
		ASSERT(result == cgltf_result_success);

		cgltf_result buffer_result = cgltf_load_buffers(&options, scene_data, importer->file_path);
		ASSERT(buffer_result == cgltf_result_success);

		ASSERT(scene_data->scene->nodes_count == 1);
		cgltf_node* root_node = scene_data->scene->nodes[0];
		cgltf_mesh* mesh = root_node->mesh;

		ASSERT(mesh->primitives_count == 1);
		cgltf_primitive* prim = &mesh->primitives[0];
		cgltf_accessor* indices = prim->indices; 
	

		Memory::Arena* mesh_memory = importer->mesh_memory;

		imported.num_indices = indices->count;
		imported.index_buffer = Memory::PushType<Gfx::Index_t>(mesh_memory, indices->count);
		CopyBuffer((u8*)imported.index_buffer, sizeof(Gfx::Index_t) * indices->count, cgltf_type_scalar, cgltf_component_type_r_16u, indices);
		
		FlipTriangleWinding(imported.index_buffer, indices->count);

		for (u64 attrib_idx = 0; attrib_idx < prim->attributes_count; ++attrib_idx)
		{
			cgltf_attribute* attrib = &prim->attributes[attrib_idx];
			cgltf_accessor* access = attrib->data;

			ASSERT_F(access->offset == 0 && access->buffer_view->stride == 0,
				"Mesh contains interleaved vertex buffer, this is not currently supported!");
			
			switch (attrib->type)
			{
				case cgltf_attribute_type_position:
				{
					imported.num_vertices = access->count;

					u64 bytes_to_alloc = sizeof(Gfx::Position_t) * access->count;
					u64 alignment = alignof(Gfx::Position_t);

					void* attrib_buffer = Memory::PushSize(mesh_memory, bytes_to_alloc, Memory::AlignPush(alignment));
					CopyBuffer((u8*)attrib_buffer, bytes_to_alloc, cgltf_type_vec3, cgltf_component_type_r_32f, access);
					
					imported.position_buffer = (Gfx::Position_t*)attrib_buffer;
					ChangeBasis(imported.position_buffer, access->count);
					
					break;
				}
				case cgltf_attribute_type_normal:
				{
					imported.num_vertices = access->count;

					u64 bytes_to_alloc = sizeof(Gfx::Normal_t) * access->count;
					u64 alignment = alignof(Gfx::Normal_t);

					void* attrib_buffer = Memory::PushSize(mesh_memory, bytes_to_alloc, Memory::AlignPush(alignment));
					CopyBuffer((u8*)attrib_buffer, bytes_to_alloc, cgltf_type_vec3, cgltf_component_type_r_32f, access);

					imported.normal_buffer = (Gfx::Normal_t*)attrib_buffer;
					ChangeBasis(imported.normal_buffer, access->count);

					break;
				}
				case cgltf_attribute_type_texcoord:
				{
					imported.num_vertices = access->count;

					u64 bytes_to_alloc = sizeof(Gfx::TexCoord_t) * access->count;
					u64 alignment = alignof(Gfx::TexCoord_t);

					void* attrib_buffer = Memory::PushSize(mesh_memory, bytes_to_alloc, Memory::AlignPush(alignment));
					CopyBuffer((u8*)attrib_buffer, bytes_to_alloc, cgltf_type_vec2, cgltf_component_type_r_32f, access);

					imported.texcoord_buffer = (Gfx::TexCoord_t*)attrib_buffer;
					break;
				}
				default:
					ASSERT_FAIL_F("Unsupported vertex attribute!");
					break;
			}
		}

		ASSERT(imported.position_buffer != nullptr);

		// TODO(): Remap texcoords

		if (result == cgltf_result_success)
		{
			cgltf_free(scene_data);
		}

		return imported;
	}
}