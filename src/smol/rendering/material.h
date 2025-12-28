#pragma once
#include "smol/asset.h"
#include "smol/asset/shader.h"
#include "smol/asset/texture.h"
#include "smol/defines.h"
#include "smol/log.h"
#include "smol/math_util.h"
#include "smol/rendering/ubo.h"

#include <cstring>
#include <memory>
#include <variant>
#include <vector>

namespace smol
{
    struct texture_binding_t
    {
        i32 location;
        u32 id;
    };

    struct material_t
    {
        smol::shader_asset_t shader;
        smol::ubo_t ubo;

        std::vector<u8> parameter_buf;
        std::vector<texture_binding_t> active_textures;

        void init(const smol::shader_asset_t& shader);

        template<typename T>
        void set_parameter(const std::string& name, const T& value)
        {
            auto it = shader.shader_data->reflection.material_fields.find(name);
            if (it == shader.shader_data->reflection.material_fields.end())
            {
                SMOL_LOG_WARN("MATERIAL", "Parameter '{}' does not exist in shader", name);
                return;
            }

            const shader_field_t& field = it->second;

            if (sizeof(T) != field.size)
            {
                SMOL_LOG_ERROR("MATERIAL", "Shader expects {}, but got {} ({})", field.size, sizeof(T), name);
                return;
            }

            if (parameter_buf.size() < field.offset + field.size) { return; }
            std::memcpy(parameter_buf.data() + field.offset, &value, sizeof(T));
            ubo.update(parameter_buf.data(), parameter_buf.size());
        }

        void set_texture(const std::string& name, const smol::texture_asset_t& texture)
        {
            if (!shader.ready() || !texture.ready()) { return; }

            auto it = shader.shader_data->reflection.material_fields.find(name);
            if (it == shader.shader_data->reflection.material_fields.end())
            {
                SMOL_LOG_WARN("MATERIAL", "Texture'{}' does not exist in shader", name);
                return;
            }

            const shader_field_t& field = it->second;

            if (field.type != shader_data_type_e::TEXTURE)
            {
                SMOL_LOG_WARN("MATERIAL", "'{}' is not a texture field", name);
                return;
            }

            for (texture_binding_t& binding : active_textures)
            {
                if (binding.location == field.location)
                {
                    binding.id = texture.tex_data->id;
                    return;
                }
            }

            active_textures.push_back({field.location, texture.tex_data->id});
        }
    };
} // namespace smol