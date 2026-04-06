#pragma once
#include "smol/asset.h"
#include "smol/assets/shader.h"
#include "smol/assets/texture.h"
#include "smol/defines.h"
#include "smol/log.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/samplers.h"

#include <climits>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace smol
{
    constexpr u32_t NULL_SHADER_MODULE = UINT_MAX;

    struct SMOL_API material_t
    {
        asset_t<shader_t> shader;
        std::vector<u8> data;

        std::unordered_map<u32_t, asset_t<texture_t>> bound_textures;
        u32_t type_id;
        u32_t shader_module_idx = NULL_SHADER_MODULE;

        u32_t heap_offset[renderer::MAX_FRAMES_IN_FLIGHT];
        u32_t dirty_frames = renderer::MAX_FRAMES_IN_FLIGHT;
        u32_t last_synced_frame = renderer::BINDLESS_NULL_HANDLE;

        material_t() = default;
        material_t(const std::string& shader_name);

        void sync();

        template <typename T>
        void set_property(u32_t name_hash, const T& value)
        {
            if (!shader || shader_module_idx == NULL_SHADER_MODULE) { return; }

            const std::unordered_map<u32_t, shader_member_t>& members = shader->modules[shader_module_idx].members;
            auto it = members.find(name_hash);

            if (it == members.end())
            {
                SMOL_LOG_WARN("MATERIAL", "Property '{}' not found in shader", name_hash);
                return;
            }

            const shader_member_t& member = it->second;

            if (sizeof(T) != member.size)
            {
                SMOL_LOG_ERROR("MATERIAL", "Size mismatch for '{}', Expected {} bytes, but got {} bytes", name_hash,
                               member.size, sizeof(T));
                return;
            }

            std::memcpy(data.data() + member.offset, &value, sizeof(T));
            dirty_frames = renderer::MAX_FRAMES_IN_FLIGHT;
        }

        void set_texture(u32_t name_hash, const asset_t<texture_t>& tex)
        {
            if (tex)
            {
                set_property<u32_t>(name_hash, tex->bindless_id);
                bound_textures[name_hash] = tex;
            }
        }

        void set_sampler(u32_t name_hash, sampler_type_e sampler)
        { set_property<u32_t>(name_hash, static_cast<u32_t>(sampler)); }
    };

    template <>
    struct SMOL_API asset_loader_t<material_t>
    {
        static std::optional<material_t> load(const std::string& path, const std::string& shader_name);
        static void unload(material_t& mat);
    };
} // namespace smol