#pragma once

#include "tau/asset_handle.h"
#include "tau/defines.h"
#include "tau/hash.h"

#include <string>
#include <string_view>
#include <vector>

namespace tau::asset_meta
{
    TAU_ENGINE_API void load_guid_map(const std::string& guid_map_path);
    // seed the in memory map for the cooker, which resolves references before any map file exists
    TAU_ENGINE_API void load_guid_map_json(const std::string& json_text);
    TAU_ENGINE_API void shutdown();

    TAU_ENGINE_API std::string_view get_guid(const std::string& path);

    TAU_ENGINE_API std::vector<std::string> all_paths();
    TAU_ENGINE_API std::string_view get_path_for_guid(const std::string& guid);

    TAU_ENGINE_API uuid_t resolve_uuid(const std::string& path);

    // a reference is a guid plus the path it had when written
    // the guid is the identity and finds where the asset lives now, so moves break nothing
    // the path is only a fallback for references written before guids
    TAU_ENGINE_API std::string resolve_ref(std::string_view guid, const std::string& recorded_path);

    TAU_ENGINE_API std::string generate_uuid();

    // the guid of a source file, created if it has none. foreign formats keep it in a .meta
    // materials and scenes keep a "guid" field instead, an old .meta is folded in and removed
    TAU_ENGINE_API std::string find_or_create_guid(const std::string& source_path);

    // for the editor about to write a material or scene: its existing guid, or a new one
    TAU_ENGINE_API std::string guid_for_writing(const std::string& source_path);
    TAU_ENGINE_API void write_guid_map(const std::string& output_path, const std::string& map_data_json,
                                       const std::string& replace_prefix = "");
} // namespace tau::asset_meta
