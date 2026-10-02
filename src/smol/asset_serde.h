#pragma once

#include "smol/asset_handle.h"
#include "smol/asset_registry.h"
#include "smol/defines.h"
#include "smol/hash.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace smol::asset_serde
{
    using load_fn_t = asset_handle_t (*)(asset_registry_t&, const std::string&);
    using reload_fn_t = bool (*)(asset_registry_t&, const std::string&);

    // only loader and reloader are registered here, names and extensions come from asset_table
    SMOL_ENGINE_API void reg(u64_t type_id, load_fn_t load_fn, reload_fn_t reload_fn);
    SMOL_ENGINE_API asset_handle_t load(u64_t type_id, asset_registry_t& reg, const std::string& path);

    // reruns the loader in place for an asset live at `path`, false on unknown type, nothing loaded or failure
    SMOL_ENGINE_API bool reload(u64_t type_id, asset_registry_t& reg, const std::string& path);
    SMOL_ENGINE_API std::string_view display_name(u64_t type_id);
    SMOL_ENGINE_API bool path_matches_type(u64_t type_id, std::string_view path);
} // namespace smol::asset_serde
