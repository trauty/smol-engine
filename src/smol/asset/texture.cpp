#include "texture.h"

#include "smol/log.h"
#include "smol/main_thread.h"

#include <stb/stb_image.h>

namespace smol::asset
{
    texture_asset_t::texture_asset_t(const std::string& path, texture_asset_args_t args)
    {
        i32 channels;
        u8* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);

        type = args.type;

        smol::main_thread::enqueue([this, pixels]()
                                   {
            glGenTextures(1, &texture_id);
            glBindTexture(GL_TEXTURE_2D, texture_id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            // die nächsten drei calls müssen parametrisiert werden..
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            stbi_image_free(pixels); });
    }

    texture_asset_t::~texture_asset_t()
    {
        smol::main_thread::enqueue([this]()
                                   {
            if (texture_id)
            {
                glDeleteTextures(1, &texture_id);
            } });
    }

    u32 texture_asset_t::get_texture_id() const
    {
        return texture_id;
    }
    i32 texture_asset_t::get_width() const
    {
        return width;
    }
    i32 texture_asset_t::get_height() const
    {
        return height;
    }
} // namespace smol::asset