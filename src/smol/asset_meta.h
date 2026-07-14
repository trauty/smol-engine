#pragma once

#include "smol/asset_handle.h"
#include "smol/defines.h"
#include "smol/hash.h"

#include <string>
#include <string_view>

namespace smol::asset_meta
{
    SMOL_API void init(const std::string& guid_map_path);
    SMOL_API void shutdown();

    SMOL_API std::string_view get_guid(const std::string& path);
    SMOL_API uuid_t resolve_uuid(const std::string& path);

    SMOL_API std::string generate_uuid();
    SMOL_API std::string find_or_create_guid(const std::string& source_path);
    SMOL_API void write_guid_map(const std::string& output_path, const std::string& map_data_json);
} // namespace smol::asset_meta
