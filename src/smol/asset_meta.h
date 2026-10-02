#pragma once

#include "smol/asset_handle.h"
#include "smol/defines.h"
#include "smol/hash.h"

#include <string>
#include <string_view>
#include <vector>

namespace smol::asset_meta
{
    SMOL_ENGINE_API void load_guid_map(const std::string& guid_map_path);
    // seed the in memory map for the cooker, which resolves references before any map file exists
    SMOL_ENGINE_API void load_guid_map_json(const std::string& json_text);
    SMOL_ENGINE_API void shutdown();

    SMOL_ENGINE_API std::string_view get_guid(const std::string& path);

    SMOL_ENGINE_API std::vector<std::string> all_paths();
    SMOL_ENGINE_API std::string_view get_path_for_guid(const std::string& guid);

    SMOL_ENGINE_API uuid_t resolve_uuid(const std::string& path);

    // a reference is a guid plus the path it had when written
    // the guid is the identity and finds where the asset lives now, so moves break nothing
    // the path is only a fallback for references written before guids
    SMOL_ENGINE_API std::string resolve_ref(std::string_view guid, const std::string& recorded_path);

    SMOL_ENGINE_API std::string generate_uuid();

    // the guid of a source file, created if it has none. foreign formats keep it in a .meta
    // materials and scenes keep a "guid" field instead, an old .meta is folded in and removed
    SMOL_ENGINE_API std::string find_or_create_guid(const std::string& source_path);

    // for the editor about to write a material or scene: its existing guid, or a new one
    SMOL_ENGINE_API std::string guid_for_writing(const std::string& source_path);
    SMOL_ENGINE_API void write_guid_map(const std::string& output_path, const std::string& map_data_json,
                                        const std::string& replace_prefix = "");
} // namespace smol::asset_meta
