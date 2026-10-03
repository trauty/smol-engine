#include "scene_cooker.h"

#include "tau/assets/scene.h"
#include "tau/log.h"
#include "tau/serialization.h"

#include "json/json.hpp"
#include <filesystem>
#include <fstream>

namespace tau::cooker::scene
{
    bool cook_scene(const std::string& input_path, const std::string& output_path)
    {
        TAU_LOG_INFO("SCENE_COOKER", "Cooking scene: {} -> {}", input_path, output_path);

        std::ifstream file(input_path);
        if (!file.is_open())
        {
            TAU_LOG_ERROR("SCENE_COOKER", "Failed to open scene file: {}", input_path);
            return false;
        }

        nlohmann::json scene_json = nlohmann::json::parse(file, nullptr, false);
        if (scene_json.is_discarded())
        {
            TAU_LOG_ERROR("SCENE_COOKER", "Scene file is not valid JSON: {}", input_path);
            return false;
        }

        tau::scene_t scene = tau::serialization::scene_from_json(scene_json);

        std::filesystem::create_directories(std::filesystem::path(output_path).parent_path());
        tau::serialization::write_scene_binary(scene, output_path);
        return true;
    }
} // namespace tau::cooker::scene
