#include "texture.h"

#include "smol/asset.h"
#include "smol/defines.h"
#include "smol/log.h"
#include "smol/main_thread.h"
#include "smol/rendering/renderer.h"

#include <optional>
#include <stb/stb_image.h>
#include <vector>

namespace smol
{
    texture_render_data_t::~texture_render_data_t()
    {
        u32 tex_id = id;

        if (tex_id != 0)
        {
            smol::main_thread::enqueue([tex_id]() { glDeleteTextures(1, &tex_id); });
        }
    }

    std::optional<texture_asset_t> asset_loader_t<texture_asset_t>::load(const std::string& path, texture_type_e type)
    {
        i32 width, height, channels;
        u8* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);

        if (!pixels)
        {
            SMOL_LOG_ERROR("TEXTURE", "Failed to load image: {}", path);
            return std::nullopt;
        }

        size_t data_size = width * height * 4;
        std::vector<u8> pixel_data(pixels, pixels + data_size);

        stbi_image_free(pixels);

        texture_asset_t tex_asset;
        tex_asset.width = width;
        tex_asset.height = height;
        tex_asset.type = type;

        smol::main_thread::enqueue([tex_data = tex_asset.tex_data, pixels = std::move(pixel_data), width, height]() {
            glGenTextures(1, &tex_data->id);
            glBindTexture(GL_TEXTURE_2D, tex_data->id);

            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            glBindTexture(GL_TEXTURE_BINDING_2D, 0);
        });

        return tex_asset;
    }
} // namespace smol