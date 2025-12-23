#pragma once

#include "smol/asset.h"
#include "smol/color.h"
#include "smol/defines.h"
#include "smol/math_util.h"
#include "smol/rendering/renderer.h"

#include <memory>
#include <optional>
#include <variant>

namespace smol
{
    enum class shader_stage_e;

    using uniform_value_t =
        std::variant<i32, f32, smol::math::vec3_t, smol::math::vec4_t, smol::color_t, smol::math::mat4_t>;

    struct shader_render_data_t
    {
        u32 program_id;
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

        void set_uniform(const std::string& name, const uniform_value_t& value) const;
        void bind_texture(const std::string& name, u32 tex_id, u32 slot) const;
    };

    template<>
    struct asset_loader_t<shader_asset_t>
    {
        static std::optional<shader_asset_t> load(const std::string& path);
    };
} // namespace smol
