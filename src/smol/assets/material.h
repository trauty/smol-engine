#pragma once
#include "smol/asset.h"
#include "smol/assets/shader.h"
#include "smol/assets/texture.h"
#include "smol/defines.h"
#include "smol/log.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/samplers.h"

#include <cstring>
#include <unordered_map>
#include <vector>

namespace smol
{
    struct SMOL_API material_t
    {
        asset_t<shader_t> shader;
        std::vector<u8> data;

        std::vector<asset_t<texture_t>> bound_textures;
        u32_t type_id;
        shader_module_info_t* shader_info;

        u32_t heap_offset = renderer::BINDLESS_NULL_HANDLE;

        bool is_dirty = true;

        material_t() = default;
        material_t(material_t&&) noexcept = default;
        material_t& operator=(material_t&&) noexcept = default;

        material_t(const std::string& shader_name);

        void sync();

        template <typename T>
        void set_property(const std::string& name, const T& value)
        {
            if (!shader) { return; }

            const std::unordered_map<std::string, shader_member_t>& members = shader_info->members;
            auto it = members.find(name);

            if (it == members.end())
            {
                SMOL_LOG_WARN("MATERIAL", "Property '{}' not found in shader", name);
                return;
            }

            const shader_member_t& member = it->second;

            if (sizeof(T) != member.size)
            {
                SMOL_LOG_ERROR("MATERIAL", "Size mismatch for '{}', Expected {} bytes, but got {} bytes", name,
                               member.size, sizeof(T));
                return;
            }

            std::memcpy(data.data() + member.offset, &value, sizeof(T));
            is_dirty = true;
        }

        void set_texture(const std::string& name, const asset_t<texture_t>& tex)
        {
            if (tex)
            {
                set_property<u32_t>(name, tex->bindless_id);
                bound_textures.push_back(tex);
            }
        }

        void set_sampler(const std::string& name, sampler_type_e sampler)
        { set_property<u32_t>(name, static_cast<u32_t>(sampler)); }
    };

    template <>
    struct SMOL_API asset_loader_t<material_t>
    {
        static std::optional<material_t> load(const std::string& path, const std::string& shader_name);
        static void unload(material_t& mat);
    };
} // namespace smol