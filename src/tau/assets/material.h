#pragma once
#include "tau/asset.h"
#include "tau/assets/shader.h"
#include "tau/assets/texture.h"
#include "tau/containers/flat_map.h"
#include "tau/defines.h"
#include "tau/engine.h"
#include "tau/log.h"
#include "tau/rendering/renderer_constants.h"
#include "tau/rendering/samplers.h"

#include <climits>
#include <cstring>
#include <string_view>
#include <vector>

namespace tau
{
    constexpr u32_t NULL_SHADER_MODULE = UINT_MAX;

    struct TAU_ENGINE_API material_t
    {
        static constexpr std::string_view type_key = "material";

        asset_handle_t shader_handle;
        std::vector<u8> data;

        std::vector<u32_t> authored_properties;

        flat_map_t<asset_handle_t> bound_textures;

        // a texture the file names that failed to load draws as the fallback, the original reference is kept
        // so saving writes what the file said. rebinding or clearing the slot forgets it
        flat_map_t<std::string> missing_textures;

        u32_t heap_offset[renderer::MAX_FRAMES_IN_FLIGHT];
        u32_t dirty_frames = renderer::MAX_FRAMES_IN_FLIGHT;
        u32_t last_synced_frame = renderer::BINDLESS_NULL_HANDLE;

        material_t() = default;
        material_t(asset_handle_t target_shader);

        void sync();

        template <typename T>
        void set_property(u32_t name_hash, const T& value)
        {
            shader_t* shader = tau::engine::get_asset_registry().get<shader_t>(shader_handle);
            if (!shader) { return; }

            const flat_map_t<shader_member_t>& members = shader->module.members;
            const shader_member_t* it = members.find(name_hash);

            if (it == nullptr)
            {
                TAU_LOG_WARN("MATERIAL", "Property {} not found in shader '{}'", name_hash, shader->module.name);
                return;
            }

            const shader_member_t& member = *it;

            if (sizeof(T) != member.size)
            {
                TAU_LOG_ERROR("MATERIAL", "Size mismatch for '{}', Expected {} bytes, but got {} bytes", name_hash,
                              member.size, sizeof(T));
                return;
            }

            std::memcpy(data.data() + member.offset, &value, sizeof(T));
            dirty_frames = renderer::MAX_FRAMES_IN_FLIGHT;
        }

        // every bound_textures entry is a reference the material owns, released on rebind, clear or unload
        // set_texture takes over a caller held reference, even when the texture did not load
        void set_texture(u32_t name_hash, asset_handle_t tex_handle);
        void clear_texture(u32_t name_hash);

        // rereads the bindless id of bound textures reloaded in place, touches no reference counts
        void refresh_texture(u32_t name_hash);

        void set_property_raw(u32_t name_hash, const void* value, u32_t size);

        bool has_property(u32_t name_hash) const;

        template <typename T>
        void set_property_if_present(u32_t name_hash, const T& value)
        {
            if (has_property(name_hash)) { set_property(name_hash, value); }
        }

        void release_heap();

        void set_sampler(u32_t name_hash, sampler_type_e sampler)
        { set_property<u32_t>(name_hash, static_cast<u32_t>(sampler)); }
    };

    template <>
    struct TAU_ENGINE_API asset_loader_t<material_t>
    {
        static std::optional<material_t> load(const std::string& path);
        static void unload(material_t& mat);
    };
} // namespace tau