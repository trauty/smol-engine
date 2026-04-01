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

    bool asset_cache_t::needs_cooking(const std::string& out_path, const std::vector<std::filesystem::path>& deps)
    {
        if (!std::filesystem::exists(out_path)) { return true; }

        std::string combined_hash = "";
        for (size_t i = 0; i < deps.size(); i++)
        {
            combined_hash += std::to_string(hash_file(deps[i]));
            if (i < deps.size() - 1) { combined_hash += "_"; }
        }

        if (!data.contains(out_path) || data[out_path]["hash"] != combined_hash) { return true; }

        return false;
    }

    void asset_cache_t::update_cache(const std::string& out_path, const std::vector<std::filesystem::path>& deps)
    {
        std::string combined_hash = "";
        for (size_t i = 0; i < deps.size(); i++)
        {
            combined_hash += std::to_string(hash_file(deps[i]));
            if (i < deps.size() - 1) { combined_hash += "_"; }
        }
        data[out_path]["hash"] = combined_hash;
    }
} // namespace smol::cooker