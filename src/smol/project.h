#pragma once

#include "defines.h"

#include <filesystem>
#include <string>

namespace smol
{
    constexpr int SMOL_PROJECT_VERSION = 2;

    struct project_t
    {
        std::string project_name;
        std::string game_lib_name;

        int project_version = 0;

        std::string startup_scene;

        std::filesystem::path project_dir;
        std::filesystem::path bin_dir;
        std::filesystem::path assets_dir;
        std::filesystem::path cooked_assets_dir;
        std::filesystem::path lib_path;
        std::filesystem::path trigger_path;

        SMOL_ENGINE_API static bool load(const std::filesystem::path& project_file, project_t& out);
    };
} // namespace smol
