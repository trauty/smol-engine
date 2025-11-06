#pragma once
#include "asset.h"
#include "smol/color.h"
#include "smol/defines.h"
#include "smol/math_util.h"

#include <variant>

using GLuint = u32;
using GLenum = u32;

namespace smol::renderer
{
    enum class shader_stage_e;
}

namespace smol::asset
{
    class texture_asset_t;

    using uniform_value_t = std::variant<
        i32,
        f32,
        smol::math::vec3_t,
        smol::math::vec4_t,
        smol::color_t,
        smol::math::mat4_t>;

    class shader_asset_t : public asset_t
    {
      public:
        // derzeit nur vertex und fragment shader, keine zeit für hull/compute/geometry
        shader_asset_t(const std::string& packed_path);
        ~shader_asset_t();

        smol_result_e compile_shader(const std::string& src, GLenum type, GLuint& out_shader);
        void bind();
        void set_uniform(const std::string& name, const uniform_value_t& value) const;
        GLuint get_program_id() const;
        void bind_texture(const std::string& name, const texture_asset_t& texture, GLuint unit);

      private:
        GLuint program_id;
    };
} // namespace smol::asset
