#pragma once
#include "smol/asset.h"
#include "smol/assets/shader.h"
#include "smol/assets/texture.h"
#include "smol/defines.h"
#include "smol/log.h"
#include "smol/rendering/renderer_types.h"

#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace smol
{
    struct material_t
    {
        asset_t<shader_t> shader;
        std::vector<u8> data;

        u32_t heap_offset = renderer::NULL_HANDLE;

        bool is_dirty = true;

        material_t(asset_t<shader_t> shader_asset) : shader(shader_asset)
        {
            if (!shader || !shader->ready())
            {
                SMOL_LOG_ERROR("MATERIAL", "Shader is not valid, can't create material");
                return;
            }

            u32_t total_size = shader->get_data()->reflection.get_ubo_size(0, 0);
            data.resize(total_size, 0);
        }

        template <typename T>
        void set_property(const std::string& name, const T& value)
        {
            if (!shader) { return; }

            const auto& members = shader->get_data()->reflection.uniform_members;
            auto it = members.find(name);

            if (it == members.end())
            {
                SMOL_LOG_WARN("MATERIAL", "Property '{}' not found in shader", name);
                return;
            }

            const shader_uniform_member_t& member = it->second;

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
            if (tex) { set_property<u32_t>(name, tex->bindless_id); }
        }
    };
} // namespace smol