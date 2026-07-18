#include "scene_cooker.h"

#include "smol/assets/scene.h"
#include "smol/log.h"
#include "smol/serialization.h"

#include "json/json.hpp"
#include <filesystem>
#include <fstream>

namespace smol::cooker::scene
{
    void cook_scene(const std::string& input_path, const std::string& output_path)
    {
        SMOL_LOG_INFO("SCENE_COOKER", "Cooking scene: {} -> {}", input_path, output_path);

        std::ifstream file(input_path);
        if (!file.is_open())
        {
            SMOL_LOG_ERROR("SCENE_COOKER", "Failed to open scene file: {}", input_path);
            return;
        }

        nlohmann::json scene_json = nlohmann::json::parse(file, nullptr, false);
        if (scene_json.is_discarded())
        {
            SMOL_LOG_ERROR("SCENE_COOKER", "Scene file is not valid JSON: {}", input_path);
            return;
        }

        smol::scene_t scene = smol::serialization::scene_from_json(scene_json);

        std::filesystem::create_directories(std::filesystem::path(output_path).parent_path());
        smol::serialization::write_scene_binary(scene, output_path);
    }
} // namespace smol::cooker::scene
