#include "material_cooker.h"

#include "smol/assets/material_format.h"
#include "smol/hash.h"
#include "smol/log.h"

#include "json/json.hpp"
#include <filesystem>
#include <fstream>
#include <vector>

namespace smol::cooker::material
{
    void cook_material(const std::string& input_path, const std::string& output_path)
    {
        SMOL_LOG_INFO("MATERIAL_COOKER", "Cooking material: {} -> {}", input_path, output_path);

        std::ifstream file(input_path);
        if (!file.is_open())
        {
            SMOL_LOG_ERROR("MATERIAL_COOKER", "Failed to open material file: {}", input_path);
            return;
        }

        nlohmann::json mat_json;
        file >> mat_json;

        std::filesystem::create_directories(std::filesystem::path(output_path).parent_path());
        std::ofstream out(output_path, std::ios::binary);

        std::string shader_path = mat_json.value("shader", "");

        material_header_t header = {
            .shader_path_length = static_cast<u32_t>(shader_path.size()),
            .texture_count = static_cast<u32_t>(mat_json.contains("textures") ? mat_json["textures"].size() : 0),
            .property_count = static_cast<u32_t>(mat_json.contains("properties") ? mat_json["properties"].size() : 0),
        };

        out.write(reinterpret_cast<const char*>(&header), sizeof(material_header_t));
        out.write(shader_path.data(), shader_path.size());

        if (header.texture_count > 0)
        {
            for (auto& [tex_name, tex_path_json] : mat_json["textures"].items())
            {
                std::string tex_path = tex_path_json.get<std::string>();

                cooked_texture_bind_t tex_bind;
                tex_bind.name_hash = smol::hash_string(tex_name);
                tex_bind.path_length = static_cast<u32_t>(tex_path.size());

                out.write(reinterpret_cast<const char*>(&tex_bind), sizeof(cooked_texture_bind_t));
                out.write(tex_path.data(), tex_path.size());
            }
        }

        if (header.property_count > 0)
        {
            for (auto& [prop_name, prop_val] : mat_json["properties"].items())
            {
                cooked_property_t prop;
                prop.name_hash = smol::hash_string(prop_name);

                if (prop_val.is_number_float())
                {
                    prop.data_size = sizeof(f32);
                    f32 val = prop_val.get<f32>();
                    out.write(reinterpret_cast<const char*>(&prop), sizeof(cooked_property_t));
                    out.write(reinterpret_cast<const char*>(&val), prop.data_size);
                }
                else if (prop_val.is_array())
                {
                    prop.data_size = prop_val.size() * sizeof(f32);
                    std::vector<f32> val_array;
                    for (auto& val : prop_val) { val_array.push_back(val.get<f32>()); }

                    out.write(reinterpret_cast<const char*>(&prop), sizeof(cooked_property_t));
                    out.write(reinterpret_cast<const char*>(val_array.data()), prop.data_size);
                }
            }
        }
    }
} // namespace smol::cooker::material