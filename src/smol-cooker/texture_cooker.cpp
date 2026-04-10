#include "texture_cooker.h"

#include "smol/defines.h"
#include "smol/log.h"
#include "smol/rendering/vulkan.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <json/json.hpp>
#include <ktx.h>
#include <stb/stb_image.h>
#include <stb/stb_image_resize2.h>
#include <string>
#include <vector>

namespace smol::cooker::texture
{
    std::string to_lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    void cook_texture(const std::string& input_path, const std::string& output_path)
    {
        SMOL_LOG_INFO("TEXTURE_COOKER", "Cooking texture: {} -> {}", input_path, output_path);

        std::string meta_path = input_path + ".meta";
        nlohmann::json meta;

        if (std::filesystem::exists(meta_path))
        {
            std::ifstream file(meta_path);
            file >> meta;
        }
        else
        {
            std::string lower_path = to_lower(input_path);
            bool is_normal =
                lower_path.find("_normal") != std::string::npos || lower_path.find("_n.") != std::string::npos;
            bool is_data =
                lower_path.find("_orm") != std::string::npos || lower_path.find("_mask") != std::string::npos ||
                lower_path.find("_roughness") != std::string::npos || lower_path.find("_metallic") != std::string::npos;
            bool is_color = !is_normal && !is_data;

            meta["type"] = is_normal ? "normal" : (is_data ? "data" : "color");
            meta["srgb"] = (!is_normal && !is_data);

            std::ofstream file(meta_path);
            file << meta.dump(4);

            SMOL_LOG_INFO("TEXTURE_COOKER", "Generated default metadata: {}", meta_path);
        }

        std::string tex_type = meta.value("type", "color");
        bool is_srgb = meta.value("srgb", true);

        i32 width, height, channels;
        stbi_uc* pixels = stbi_load(input_path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels)
        {
            SMOL_LOG_ERROR("TEXTURE_COOKER", "Failed to load texture: {}", input_path);
            return;
        }

        u32_t mip_count = static_cast<u32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

        ktxTexture2* tex;
        ktxTextureCreateInfo tex_create_info = {
            .vkFormat = static_cast<u32_t>(is_srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM),
            .baseWidth = static_cast<u32_t>(width),
            .baseHeight = static_cast<u32_t>(height),
            .baseDepth = 1,
            .numDimensions = 2,
            .numLevels = mip_count,
            .numLayers = 1,
            .numFaces = 1,
            .isArray = KTX_FALSE,
            .generateMipmaps = KTX_FALSE,
        };

        if (ktxTexture2_Create(&tex_create_info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &tex) != KTX_SUCCESS)
        {
            SMOL_LOG_ERROR("TEXTURE_COOKER", "Failed to create ktx2 texture: {}", input_path);
            stbi_image_free(pixels);
            return;
        }

        u32_t mip_w = width;
        u32_t mip_h = height;
        std::vector<u8_t> cur_mip_data(width * height * 4);
        std::memcpy(cur_mip_data.data(), pixels, width * height * 4);

        for (u32_t i = 0; i < mip_count; i++)
        {
            ktxTexture_SetImageFromMemory(ktxTexture(tex), i, 0, 0, cur_mip_data.data(), mip_w * mip_h * 4);

            if (i < mip_count - 1)
            {
                u32_t next_w = std::max(1u, mip_w / 2);
                u32_t next_h = std::max(1u, mip_h / 2);
                std::vector<u8_t> next_mip_data(next_w * next_h * 4);

                if (is_srgb)
                {
                    stbir_resize_uint8_srgb(cur_mip_data.data(), mip_w, mip_h, 0, next_mip_data.data(), next_w, next_h,
                                            0, stbir_pixel_layout::STBIR_RGBA);
                }
                else
                {
                    stbir_resize_uint8_linear(cur_mip_data.data(), mip_w, mip_h, 0, next_mip_data.data(), next_w,
                                              next_h, 0, stbir_pixel_layout::STBIR_RGBA);
                }

                cur_mip_data = std::move(next_mip_data);
                mip_w = next_w;
                mip_h = next_h;
            }
        }

        ktxTexture_SetImageFromMemory(ktxTexture(tex), 0, 0, 0, pixels, width * height * 4);
        stbi_image_free(pixels);

        ktxHashList_AddKVPair(&tex->kvDataHead, "smol_tex_type", static_cast<u32_t>(tex_type.length()) + 1,
                              tex_type.c_str());

        std::string chan_str = std::to_string(channels);
        ktxHashList_AddKVPair(&tex->kvDataHead, "smol_tex_channels", static_cast<u32_t>(chan_str.length()) + 1,
                              chan_str.c_str());

        ktxBasisParams params = {0};
        params.structSize = sizeof(params);
        params.threadCount = 2;

        if (tex_type == "color")
        {
            params.uastc = KTX_FALSE;
            params.compressionLevel = KTX_ETC1S_DEFAULT_COMPRESSION_LEVEL;
        }
        else if (tex_type == "normal")
        {
            params.uastc = KTX_TRUE;
            params.normalMap = KTX_TRUE;
        }
        else
        {
            params.uastc = KTX_TRUE;
            params.normalMap = KTX_FALSE;
        }

        if (ktx_error_code_e res = ktxTexture2_CompressBasisEx(tex, &params); res != KTX_SUCCESS)
        {
            SMOL_LOG_ERROR("TEXTURE_COOKER", "Failed to compress ktx2 texture '{}': {}", input_path,
                           ktxErrorString(res));
            return;
        }

        std::filesystem::create_directories(std::filesystem::path(output_path).parent_path());
        if (ktxTexture_WriteToNamedFile(ktxTexture(tex), output_path.c_str()) != KTX_SUCCESS)
        {
            SMOL_LOG_ERROR("TEXTURE_COOKER", "Failed to save ktx2 texture '{}' to '{}'", input_path, output_path);
        }

        ktxTexture_Destroy(ktxTexture(tex));
    }
} // namespace smol::cooker::texture