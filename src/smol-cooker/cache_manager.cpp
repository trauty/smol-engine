#include "cache_manager.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace smol::cooker
{
    void asset_cache_t::load()
    {
        if (std::filesystem::exists(path))
        {
            std::ifstream in(path);
            if (in.is_open()) { in >> data; }
        }
    }

    void asset_cache_t::save()
    {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream out(path);
        out << data.dump(4);
    }

    std::string asset_cache_t::combined_dep_hash(const std::vector<std::filesystem::path>& deps)
    {
        std::string combined_hash = std::to_string(COOKER_VERSION);
        for (const std::filesystem::path& dep : deps) { combined_hash += "_" + std::to_string(hash_file(dep)); }
        return combined_hash;
    }

    bool asset_cache_t::needs_cooking(const std::string& out_path, const std::vector<std::filesystem::path>& deps)
    {
        if (!std::filesystem::exists(out_path)) { return true; }

        if (!data.contains(out_path) || data[out_path]["hash"] != combined_dep_hash(deps)) { return true; }

        return false;
    }

    void asset_cache_t::update_cache(const std::string& out_path, const std::vector<std::filesystem::path>& deps)
    { data[out_path]["hash"] = combined_dep_hash(deps); }
} // namespace smol::cooker