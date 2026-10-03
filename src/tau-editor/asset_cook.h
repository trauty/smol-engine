#pragma once

#include "tau/os.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace tau::editor::asset_cook
{
    void set_project(const std::string& project_file);

    // reimport one source, costs a process start rather than a project walk. out_err is the cooker's error line
    bool cook_asset(const std::filesystem::path& source_file, std::string& out_err);

    // the same for a batch in one process, as the cost is almost all process start
    // also cooks whatever was built from the named files
    // out_cooked receives every source cooked, even when part of the batch failed
    bool cook_assets(std::span<const std::filesystem::path> source_files, std::string& out_err,
                     std::vector<std::filesystem::path>* out_cooked = nullptr);

    // the same for the engine's own assets, into the tree this editor loads engine:// from
    bool cook_engine_assets(std::span<const std::filesystem::path> source_files, std::string& out_err,
                            std::vector<std::filesystem::path>* out_cooked = nullptr);

    // where the engine's asset sources are, or empty if unknown
    std::filesystem::path engine_assets_dir();

    // full cook: scan, cook what is stale, sweep orphans, rewrite the guid map. runs on open
    // `cancel` stops it from another thread, cooker output is relayed into the log
    bool cook_project(std::string& out_err, tau::os::process_cancel_t* cancel = nullptr);
} // namespace tau::editor::asset_cook
