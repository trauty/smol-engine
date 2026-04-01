#pragma once

#include "smol/defines.h"

#include "json/json.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
#define XXH_INLINE_ALL
#include "xxhash.h"

namespace smol::cooker
{
    inline u64_t hash_file(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) { return 0; }

        XXH3_state_t* state = XXH3_createState();
        if (!state) { return 0; }

        XXH3_64bits_reset(state);

        constexpr size_t BUFFER_SIZE = 64 * 1024;
        char buffer[BUFFER_SIZE];

        while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
        {
            XXH3_64bits_update(state, buffer, file.gcount());
        }

        u64_t final_hash = XXH3_64bits_digest(state);
        XXH3_freeState(state);

        return final_hash;
    }

    struct asset_cache_t
    {
        nlohmann::json data;
        std::string path;

        asset_cache_t(const std::string& path) : path(path) {}

        void load();
        void save();

        bool needs_cooking(const std::string& out_path, const std::vector<std::filesystem::path>& deps);
        void update_cache(const std::string& out_path, const std::vector<std::filesystem::path>& deps);
    };
} // namespace smol::cooker