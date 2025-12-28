#pragma once

#include "smol/asset.h"
#include "smol/color.h"
#include "smol/defines.h"
#include "smol/math_util.h"
#include "smol/rendering/renderer.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace smol
{
    enum class shader_stage_e;

    enum class shader_data_type_e
    {
        FLOAT,
        FLOAT2,
        FLOAT3,
        FLOAT4,
        MAT4,
        INT,
        TEXTURE,
        UNDEFINED
    };

    struct shader_field_t
    {
        std::string name;
        shader_data_type_e type;

        u32 ubo_binding = 0;
        size_t offset = 0;
        size_t size = 0;

        // unfortunately opengl 3.3 doesn't support explicit bindings...(Vulkan renderer when???)
        u32 tex_unit = 0;
        i32 location = -1;
    };

    struct shader_reflection_t
    {
        std::unordered_map<std::string, shader_field_t> material_fields;
        std::unordered_map<u32, size_t> ubo_sizes;

        bool has_field(const std::string& name) const { return material_fields.find(name) != material_fields.end(); }
    };

    struct shader_render_data_t
    {
        u32 program_id;

        shader_reflection_t reflection;

        ~shader_render_data_t();
    };

    struct shader_asset_t
    {
        std::shared_ptr<shader_render_data_t> shader_data = std::make_shared<shader_render_data_t>();

        bool ready() const { return shader_data->program_id != 0; }

        // only call on main thread
        void bind() const
        {
            if (ready()) { glUseProgram(shader_data->program_id); }
        }
    };

    template<>
    struct asset_loader_t<shader_asset_t>
    {
        static std::optional<shader_asset_t> load(const std::string& path);
    };
} // namespace smol
