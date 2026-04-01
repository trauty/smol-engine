#include "texture_cooker.h"

#include "smol/log.h"
#include "vulkan/vulkan_core.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <json/json.hpp>
#include <ktx.h>
#include <stb/stb_image.h>
#include <string>

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

        ktxTexture2* tex;
        ktxTextureCreateInfo tex_create_info = {
            .vkFormat = static_cast<u32_t>(is_srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM),
            .baseWidth = static_cast<u32_t>(width),
            .baseHeight = static_cast<u32_t>(height),
            .baseDepth = 1,
            .numDimensions = 2,
            .numLevels = 1,
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