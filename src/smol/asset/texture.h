#pragma once
#include "asset.h"
#include "smol/defines.h"

#include <glad/gl.h>
#include <string>

namespace smol::asset
{
    enum class texture_type_e
    {
        ALBEDO,
        METALLIC,
        ROUGHNESS,
        NORMAL
    };

    struct texture_asset_args_t
    {
        texture_type_e type;
        texture_asset_args_t(texture_type_e type) : type(type) {}
    };

    class texture_asset_t : public asset_t
    {
      public:
        texture_asset_t(const std::string& path, texture_asset_args_t args);
        ~texture_asset_t();

        u32 get_texture_id() const;
        i32 get_width() const;
        i32 get_height() const;
        texture_type_e get_type() const;

      private:
        GLuint texture_id;
        i32 width;
        i32 height;

        texture_type_e type;
    };
} // namespace smol::asset