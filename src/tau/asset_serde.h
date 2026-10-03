#pragma once

#include "tau/asset_handle.h"
#include "tau/asset_registry.h"
#include "tau/defines.h"
#include "tau/hash.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tau::asset_serde
{
    using load_fn_t = asset_handle_t (*)(asset_registry_t&, const std::string&);
    using reload_fn_t = bool (*)(asset_registry_t&, const std::string&);

    // only loader and reloader are registered here, names and extensions come from asset_table
    TAU_ENGINE_API void reg(u64_t type_id, load_fn_t load_fn, reload_fn_t reload_fn);
    TAU_ENGINE_API asset_handle_t load(u64_t type_id, asset_registry_t& reg, const std::string& path);

    // reruns the loader in place for an asset live at `path`, false on unknown type, nothing loaded or failure
    TAU_ENGINE_API bool reload(u64_t type_id, asset_registry_t& reg, const std::string& path);
    TAU_ENGINE_API std::string_view display_name(u64_t type_id);
    TAU_ENGINE_API bool path_matches_type(u64_t type_id, std::string_view path);
} // namespace tau::asset_serde
