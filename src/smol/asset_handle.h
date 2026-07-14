#pragma once

#include "smol/defines.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace smol
{
    using uuid_t = u64_t;

    struct SMOL_API asset_handle_t
    {
        uuid_t uuid = 0;
        u32_t pool_index = UINT32_MAX;

        bool is_valid() const { return uuid != 0; }
        bool operator==(const asset_handle_t& other) const { return uuid == other.uuid; }
        operator bool() const { return is_valid(); }
    };

    inline std::string get_cooked_path(const std::string& raw_path, const std::string& extension)
    {
        std::string protocol = "";
        std::string path_to_process = raw_path;

        size_t protocol_pos = raw_path.find("://");
        if (protocol_pos != std::string::npos)
        {
            protocol = raw_path.substr(0, protocol_pos + 3);
            path_to_process = raw_path.substr(protocol_pos + 3);
        }

        std::filesystem::path cooked_path(path_to_process);
        cooked_path.replace_extension(extension);

        return protocol + cooked_path.generic_string();
    }
} // namespace smol