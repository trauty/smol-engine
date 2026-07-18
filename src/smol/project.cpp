#include "smol/project.h"

#include "smol/log.h"

#include "json/json.hpp"
#include <cstdlib>
#include <fstream>
#include <utility>

namespace
{
#if SMOL_PLATFORM_WIN
    constexpr const char* LIB_PREFIX = "";
    constexpr const char* LIB_EXT = ".dll";
#elif SMOL_PLATFORM_LINUX || SMOL_PLATFORM_ANDROID
    constexpr const char* LIB_PREFIX = "lib";
    constexpr const char* LIB_EXT = ".so";
#endif
} // namespace

namespace smol
{
    bool project_t::load(const std::filesystem::path& project_file, project_t& out)
    {
        std::ifstream file(project_file);
        if (!file.is_open())
        {
            SMOL_LOG_ERROR("PROJECT", "Could not open project file: {}", project_file.string());
            return false;
        }

        nlohmann::json data = nlohmann::json::parse(file, nullptr, false);
        if (data.is_discarded())
        {
            SMOL_LOG_ERROR("PROJECT", "Project file is not valid: {}", project_file.string());
            return false;
        }

        project_t p;
        p.project_name = data.value("project_name", "smol-game");
        p.game_lib_name = data.value("game_lib_name", "smol-game-logic");
        p.startup_scene = data.value("startup_scene", "");

        if (data.contains("smolproject_version"))
        {
            const nlohmann::json& v = data["smolproject_version"];
            if (v.is_string()) { p.project_version = std::atoi(v.get<std::string>().c_str()); }
            else if (v.is_number()) { p.project_version = v.get<int>(); }
        }
        if (p.project_version != 0 && p.project_version != SMOL_PROJECT_VERSION)
        {
            SMOL_LOG_WARN("PROJECT", "Project '{}' is schema version {} but this engine expects {}; may need migration",
                          project_file.string(), p.project_version, SMOL_PROJECT_VERSION);
        }

        nlohmann::json paths = nlohmann::json::object();
        if (data.contains("paths") && data["paths"].is_object()) { paths = data["paths"]; }

        std::string bin_dir = paths.value("bin_dir", "bin");
        std::string assets_dir = paths.value("assets_dir", "assets");
        std::string cooked_assets_dir = paths.value("cooked_assets_dir", ".smol");

        p.project_dir = std::filesystem::absolute(project_file).parent_path();
        p.bin_dir = p.project_dir / bin_dir;
        p.assets_dir = p.project_dir / assets_dir;
        p.cooked_assets_dir = p.project_dir / cooked_assets_dir;

        std::string lib_file = std::string(LIB_PREFIX) + p.game_lib_name + LIB_EXT;
        p.lib_path = p.bin_dir / lib_file;
        p.trigger_path = p.lib_path.string() + ".trigger";

        out = std::move(p);
        return true;
    }
} // namespace smol
