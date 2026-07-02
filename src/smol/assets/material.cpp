#include "material.h"

#include "entt/core/type_info.hpp"
#include "smol/asset.h"
#include "smol/asset_registry.h"
#include "smol/assets/material_format.h"
#include "smol/assets/shader.h"
#include "smol/engine.h"
#include "smol/log.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_resources.h"
#include "smol/rendering/renderer_types.h"
#include "smol/vfs.h"

#include <optional>
#include <vector>

namespace smol
{
    material_t::material_t(asset_handle_t target_shader) : shader_handle(target_shader)
    {
        shader_t* shader = smol::engine::get_asset_registry().get<shader_t>(target_shader);

        for (u32_t i = 0; i < renderer::MAX_FRAMES_IN_FLIGHT; i++) { heap_offset[i] = renderer::BINDLESS_NULL_HANDLE; }

        if (!shader)
        {
            SMOL_LOG_ERROR("MATERIAL", "Cannot create material with null shader");
            return;
        }

        if (shader->has_material_data) { data.resize(shader->module.size, 0); }
        else
        {
            SMOL_LOG_WARN("SHADER", "Shader '{}' has no material info attached", shader->module.name);
        }
    }

    void material_t::set_property_raw(u32_t name_hash, const void* value, u32_t size)
    {
        shader_t* shader = smol::engine::get_asset_registry().get<shader_t>(shader_handle);
        if (!shader) { return; }

        const auto& members = shader->module.members;
        auto it = members.find(name_hash);
        if (it == members.end())
        {
            SMOL_LOG_WARN("MATERIAL", "Property '{}' not found in shader", name_hash);
            return;
        }

        const shader_member_t& member = it->second;
        if (size != member.size)
        {
            SMOL_LOG_ERROR("MATERIAL", "Size mismatch for '{}': expected {} bytes, got {}", name_hash, member.size,
                           size);
            return;
        }

        std::memcpy(data.data() + member.offset, value, size);
        dirty_frames = renderer::MAX_FRAMES_IN_FLIGHT;
    }

    void material_t::sync()
    {
        if (dirty_frames == 0 || data.empty()) { return; }

        u32_t cur_frame = renderer::ctx.cur_frame;

        if (heap_offset[cur_frame] == renderer::BINDLESS_NULL_HANDLE)
        {
            heap_offset[cur_frame] = renderer::res_system.material_heap.allocate(data.size());
        }

        renderer::res_system.material_heap.update(heap_offset[cur_frame], data.data(), data.size());

        if (last_synced_frame != cur_frame)
        {
            dirty_frames--;
            last_synced_frame = cur_frame;
        }
    }

    std::optional<material_t> asset_loader_t<material_t>::load(const std::string& path, asset_handle_t target_shader)
    {
        if (target_shader.is_valid())
        {
            material_t mat(target_shader);
            if (!mat.shader_handle.is_valid()) { return std::nullopt; }
            return mat;
        }

        std::string cooked_path = get_cooked_path(path, ".smolmat");
        std::vector<u8_t> bytes = smol::vfs::read_bytes(cooked_path);
        if (bytes.empty())
        {
            SMOL_LOG_WARN("MATERIAL", "Material file not found: {}", cooked_path);
            return std::nullopt;
        }

        if (bytes.size() < sizeof(material_header_t))
        {
            SMOL_LOG_ERROR("MATERIAL", "Truncated material file: {}", cooked_path);
            return std::nullopt;
        }

        material_header_t* header = reinterpret_cast<material_header_t*>(bytes.data());
        if (header->magic != SMOL_MATERIAL_MAGIC)
        {
            SMOL_LOG_ERROR("MATERIAL", "Invalid material magic in: {}", cooked_path);
            return std::nullopt;
        }

        u32_t offset = sizeof(material_header_t);

        if (offset + header->shader_path_length > bytes.size())
        {
            SMOL_LOG_ERROR("MATERIAL", "Truncated shader path in: {}", cooked_path);
            return std::nullopt;
        }
        std::string shader_path(reinterpret_cast<char*>(bytes.data() + offset), header->shader_path_length);
        offset += header->shader_path_length;

        asset_handle_t shader_handle = smol::engine::get_asset_registry().load_sync<shader_t>(shader_path);
        if (!shader_handle.is_valid())
        {
            SMOL_LOG_ERROR("MATERIAL", "Failed to load shader '{}' for material", shader_path);
            return std::nullopt;
        }

        material_t mat(shader_handle);
        if (!mat.shader_handle.is_valid()) { return std::nullopt; }

        for (u32_t i = 0; i < header->texture_count; i++)
        {
            if (offset + sizeof(cooked_texture_bind_t) > bytes.size())
            {
                SMOL_LOG_ERROR("MATERIAL", "Truncated texture bind in: {}", cooked_path);
                return std::nullopt;
            }
            cooked_texture_bind_t* tex_bind = reinterpret_cast<cooked_texture_bind_t*>(bytes.data() + offset);
            offset += sizeof(cooked_texture_bind_t);

            if (offset + tex_bind->path_length > bytes.size())
            {
                SMOL_LOG_ERROR("MATERIAL", "Truncated texture path in: {}", cooked_path);
                return std::nullopt;
            }
            std::string tex_path(reinterpret_cast<char*>(bytes.data() + offset), tex_bind->path_length);
            offset += tex_bind->path_length;

            asset_handle_t tex_handle = smol::engine::get_asset_registry().load_sync<texture_t>(tex_path);
            if (tex_handle.is_valid()) { mat.set_texture(tex_bind->name_hash, tex_handle); }
            else
            {
                SMOL_LOG_WARN("MATERIAL", "Failed to load texture '{}' for material", tex_path);
            }
        }

        for (u32_t i = 0; i < header->sampler_count; i++)
        {
            if (offset + sizeof(cooked_sampler_bind_t) > bytes.size())
            {
                SMOL_LOG_ERROR("MATERIAL", "Truncated sampler bind in: {}", cooked_path);
                return std::nullopt;
            }
            cooked_sampler_bind_t* smp_bind = reinterpret_cast<cooked_sampler_bind_t*>(bytes.data() + offset);
            offset += sizeof(cooked_sampler_bind_t);

            mat.set_sampler(smp_bind->name_hash, static_cast<sampler_type_e>(smp_bind->sampler_value));
        }

        for (u32_t i = 0; i < header->property_count; i++)
        {
            if (offset + sizeof(cooked_property_t) > bytes.size())
            {
                SMOL_LOG_ERROR("MATERIAL", "Truncated property in: {}", cooked_path);
                return std::nullopt;
            }
            cooked_property_t* prop = reinterpret_cast<cooked_property_t*>(bytes.data() + offset);
            offset += sizeof(cooked_property_t);

            if (offset + prop->data_size > bytes.size())
            {
                SMOL_LOG_ERROR("MATERIAL", "Truncated property data in: {}", cooked_path);
                return std::nullopt;
            }
            mat.set_property_raw(prop->name_hash, bytes.data() + offset, prop->data_size);
            offset += prop->data_size;
        }

        return mat;
    }

    void asset_loader_t<material_t>::unload(material_t& mat)
    {
        for (u32_t i = 0; i < renderer::MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (mat.heap_offset[i] != renderer::BINDLESS_NULL_HANDLE)
            {
                renderer::res_system.material_heap.free(mat.heap_offset[i], mat.data.size());
            }
        }

        mat.data.clear();
        mat.bound_textures.clear();
        smol::engine::get_asset_registry().release<shader_t>(mat.shader_handle);
    }
}; // namespace smol