#include "material.h"

#include "entt/core/type_info.hpp"
#include "tau/asset_table.h"
#include "tau/asset.h"
#include "tau/asset_meta.h"
#include "tau/asset_registry.h"
#include "tau/assets/material_format.h"
#include "tau/assets/shader.h"
#include "tau/engine.h"
#include "tau/log.h"
#include "tau/rendering/renderer.h"
#include "tau/rendering/renderer_resources.h"
#include "tau/rendering/renderer_types.h"
#include "tau/vfs.h"

#include <optional>
#include <vector>

namespace tau
{
    material_t::material_t(asset_handle_t target_shader) : shader_handle(target_shader)
    {
        shader_t* shader = tau::engine::get_asset_registry().get<shader_t>(target_shader);

        for (u32_t i = 0; i < renderer::MAX_FRAMES_IN_FLIGHT; i++) { heap_offset[i] = renderer::BINDLESS_NULL_HANDLE; }

        if (!shader)
        {
            TAU_LOG_ERROR("MATERIAL", "Cannot create material with null shader");
            return;
        }

        if (shader->has_material_data) { data.resize(shader->module.size, 0); }
        else
        {
            TAU_LOG_WARN("SHADER", "Shader '{}' has no material info attached", shader->module.name);
        }
    }

    namespace
    {
        std::string join_member_names(const shader_t& shader)
        {
            std::string out;
            for (const auto& [hash, member] : shader.module.members)
            {
                if (!out.empty()) { out += ", "; }
                out += member.name.empty() ? "?" : member.name;
            }
            return out;
        }
    } // namespace

    bool material_t::has_property(u32_t name_hash) const
    {
        shader_t* shader = tau::engine::get_asset_registry().get<shader_t>(shader_handle);
        if (!shader) { return false; }
        return shader->module.members.contains(name_hash);
    }

    void material_t::set_property_raw(u32_t name_hash, const void* value, u32_t size)
    {
        shader_t* shader = tau::engine::get_asset_registry().get<shader_t>(shader_handle);
        if (!shader) { return; }

        const auto& members = shader->module.members;
        const shader_member_t* it = members.find(name_hash);
        if (it == nullptr)
        {
            TAU_LOG_WARN("MATERIAL", "Property {} not found in shader '{}', it has: {}", name_hash, shader->module.name,
                         join_member_names(*shader));
            return;
        }

        const shader_member_t& member = *it;
        if (size != member.size)
        {
            TAU_LOG_ERROR("MATERIAL", "Size mismatch for '{}': expected {} bytes, got {}", name_hash, member.size,
                          size);
            return;
        }

        std::memcpy(data.data() + member.offset, value, size);
        dirty_frames = renderer::MAX_FRAMES_IN_FLIGHT;
    }

    void material_t::set_texture(u32_t name_hash, asset_handle_t tex_handle)
    {
        asset_registry_t& assets = tau::engine::get_asset_registry();

        texture_t* tex = assets.get<texture_t>(tex_handle);
        if (tex == nullptr)
        {
            // nothing to bind, but the reference was handed over all the same
            assets.release<texture_t>(tex_handle);
            return;
        }

        // store before releasing: rebinding the same texture hands back the old reference, the new one keeps it alive
        asset_handle_t previous = {};
        if (asset_handle_t* bound = bound_textures.find(name_hash)) { previous = *bound; }

        bound_textures[name_hash] = tex_handle;
        missing_textures.erase(name_hash);
        set_property<u32_t>(name_hash, tex->bindless_id);

        assets.release<texture_t>(previous);
    }

    void material_t::clear_texture(u32_t name_hash)
    {
        const asset_handle_t* bound = bound_textures.find(name_hash);
        if (bound == nullptr) { return; }

        const asset_handle_t previous = *bound;
        bound_textures.erase(name_hash);
        missing_textures.erase(name_hash);
        set_property_if_present<u32_t>(name_hash, renderer::BINDLESS_NULL_HANDLE);

        tau::engine::get_asset_registry().release<texture_t>(previous);
    }

    void material_t::refresh_texture(u32_t name_hash)
    {
        const asset_handle_t* bound = bound_textures.find(name_hash);
        if (bound == nullptr) { return; }

        const texture_t* tex = tau::engine::get_asset_registry().get<texture_t>(*bound);
        if (tex != nullptr) { set_property<u32_t>(name_hash, tex->bindless_id); }
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

    void material_t::release_heap()
    {
        for (u32_t i = 0; i < renderer::MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (heap_offset[i] != renderer::BINDLESS_NULL_HANDLE)
            {
                renderer::res_system.material_heap.free(heap_offset[i], data.size());
                heap_offset[i] = renderer::BINDLESS_NULL_HANDLE;
            }
        }

        data.clear();
    }

    std::optional<material_t> asset_loader_t<material_t>::load(const std::string& path)
    {
        std::string cooked_path = tau::asset_table::cooked_path(tau::get_type_id<material_t>(), path);
        std::vector<u8_t> bytes = tau::vfs::read_bytes(cooked_path);
        if (bytes.empty())
        {
            TAU_LOG_WARN("MATERIAL", "Material file not found: {}", cooked_path);
            return std::nullopt;
        }

        if (bytes.size() < sizeof(material_header_t))
        {
            TAU_LOG_ERROR("MATERIAL", "Truncated material file: {}", cooked_path);
            return std::nullopt;
        }

        material_header_t* header = reinterpret_cast<material_header_t*>(bytes.data());
        if (header->magic != TAU_MATERIAL_MAGIC)
        {
            TAU_LOG_ERROR("MATERIAL", "Invalid material magic in: {}", cooked_path);
            return std::nullopt;
        }

        if (header->version != TAU_MATERIAL_VERSION)
        {
            TAU_LOG_ERROR("MATERIAL", "Unsupported material version {} (engine expects {}), recook: {}",
                          header->version, TAU_MATERIAL_VERSION, cooked_path);
            return std::nullopt;
        }

        u32_t offset = sizeof(material_header_t);

        if (offset + header->shader_path_length > bytes.size())
        {
            TAU_LOG_ERROR("MATERIAL", "Truncated shader path in: {}", cooked_path);
            return std::nullopt;
        }
        std::string shader_path(reinterpret_cast<char*>(bytes.data() + offset), header->shader_path_length);
        offset += header->shader_path_length;

        if (offset + header->shader_guid_length > bytes.size())
        {
            TAU_LOG_ERROR("MATERIAL", "Truncated shader guid in: {}", cooked_path);
            return std::nullopt;
        }
        std::string shader_guid(reinterpret_cast<char*>(bytes.data() + offset), header->shader_guid_length);
        offset += header->shader_guid_length;

        shader_path = tau::asset_meta::resolve_ref(shader_guid, shader_path);

        asset_handle_t shader_handle = tau::engine::get_asset_registry().load_sync<shader_t>(shader_path);

        // is_valid only says a slot was reserved, a shader that failed to compile still has one
        // ask for the shader itself, or a material loads holding a dead handle and reports success
        if (tau::engine::get_asset_registry().get<shader_t>(shader_handle) == nullptr)
        {
            TAU_LOG_ERROR("MATERIAL", "Shader '{}' did not load, so this material cannot be built", shader_path);
            return std::nullopt;
        }

        material_t mat(shader_handle);
        if (!mat.shader_handle.is_valid()) { return std::nullopt; }

        for (u32_t i = 0; i < header->texture_count; i++)
        {
            if (offset + sizeof(cooked_texture_bind_t) > bytes.size())
            {
                TAU_LOG_ERROR("MATERIAL", "Truncated texture bind in: {}", cooked_path);
                return std::nullopt;
            }
            cooked_texture_bind_t* tex_bind = reinterpret_cast<cooked_texture_bind_t*>(bytes.data() + offset);
            offset += sizeof(cooked_texture_bind_t);

            if (offset + tex_bind->path_length > bytes.size())
            {
                TAU_LOG_ERROR("MATERIAL", "Truncated texture path in: {}", cooked_path);
                return std::nullopt;
            }
            std::string tex_path(reinterpret_cast<char*>(bytes.data() + offset), tex_bind->path_length);
            offset += tex_bind->path_length;

            if (offset + tex_bind->guid_length > bytes.size())
            {
                TAU_LOG_ERROR("MATERIAL", "Truncated texture guid in: {}", cooked_path);
                return std::nullopt;
            }
            std::string tex_guid(reinterpret_cast<char*>(bytes.data() + offset), tex_bind->guid_length);
            offset += tex_bind->guid_length;

            tex_path = tau::asset_meta::resolve_ref(tex_guid, tex_path);

            asset_handle_t tex_handle = tau::engine::get_asset_registry().load_sync<texture_t>(tex_path);

            // is_valid only says a slot was made, whether it loaded is whether the texture itself is there
            if (tau::engine::get_asset_registry().get<texture_t>(tex_handle) == nullptr)
            {
                TAU_LOG_WARN("MATERIAL", "Texture '{}' did not load, binding the fallback", tex_path);

                // hand back the reference on the one that failed and take one on the fallback
                // so the material owns exactly what it binds
                tau::engine::get_asset_registry().release<texture_t>(tex_handle);
                tex_handle = tau::engine::get_asset_registry().load_sync<texture_t>(renderer::FALLBACK_TEXTURE_PATH);

                mat.set_texture(tex_bind->name_hash, tex_handle);
                mat.missing_textures[tex_bind->name_hash] = tex_path;
            }
            else
            {
                mat.set_texture(tex_bind->name_hash, tex_handle);
            }

            // the file named it, so it is authored: save_material writes only authored entries
            mat.authored_properties.push_back(tex_bind->name_hash);
        }

        for (u32_t i = 0; i < header->sampler_count; i++)
        {
            if (offset + sizeof(cooked_sampler_bind_t) > bytes.size())
            {
                TAU_LOG_ERROR("MATERIAL", "Truncated sampler bind in: {}", cooked_path);
                return std::nullopt;
            }
            cooked_sampler_bind_t* smp_bind = reinterpret_cast<cooked_sampler_bind_t*>(bytes.data() + offset);
            offset += sizeof(cooked_sampler_bind_t);

            mat.set_sampler(smp_bind->name_hash, static_cast<sampler_type_e>(smp_bind->sampler_value));
            mat.authored_properties.push_back(smp_bind->name_hash);
        }

        for (u32_t i = 0; i < header->property_count; i++)
        {
            if (offset + sizeof(cooked_property_t) > bytes.size())
            {
                TAU_LOG_ERROR("MATERIAL", "Truncated property in: {}", cooked_path);
                return std::nullopt;
            }
            cooked_property_t* prop = reinterpret_cast<cooked_property_t*>(bytes.data() + offset);
            offset += sizeof(cooked_property_t);

            if (offset + prop->data_size > bytes.size())
            {
                TAU_LOG_ERROR("MATERIAL", "Truncated property data in: {}", cooked_path);
                return std::nullopt;
            }
            mat.set_property_raw(prop->name_hash, bytes.data() + offset, prop->data_size);
            mat.authored_properties.push_back(prop->name_hash);
            offset += prop->data_size;
        }

        return mat;
    }

    void asset_loader_t<material_t>::unload(material_t& mat)
    {
        mat.release_heap();

        // each binding owns a reference, release them or every texture a material used stays alive until exit
        for (auto [name_hash, handle] : mat.bound_textures)
        {
            tau::engine::get_asset_registry().release<texture_t>(handle);
        }
        mat.bound_textures.clear();
        tau::engine::get_asset_registry().release<shader_t>(mat.shader_handle);
    }
}; // namespace tau