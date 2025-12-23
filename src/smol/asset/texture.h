#pragma once
#include "smol/asset.h"
#include "smol/asset/shader.h"
#include "smol/defines.h"

#include <glad/gl.h>
#include <memory>
#include <optional>
#include <string>

namespace smol
{
    enum class texture_type_e
    {
        ALBEDO,
        METALLIC,
        ROUGHNESS,
        NORMAL
    };

    struct texture_render_data_t
    {
        GLuint id = 0;

        ~texture_render_data_t();
    };

    struct texture_asset_t
    {
        std::shared_ptr<texture_render_data_t> tex_data = std::make_shared<texture_render_data_t>();

        i32 width = 0;
        i32 height = 0;
        texture_type_e type = texture_type_e::ALBEDO;

        bool ready() const { return tex_data->id != 0; }
    };

    template<>
    struct asset_loader_t<texture_asset_t>
    {
        static std::optional<texture_asset_t> load(const std::string& path,
                                                   texture_type_e type = texture_type_e::ALBEDO);
    };
} // namespace smol