#pragma once

#include "smol/asset_handle.h"
#include "smol/asset_registry.h"
#include "smol/defines.h"
#include "smol/hash.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace smol::asset_serde
{
    using load_fn_t = asset_handle_t (*)(asset_registry_t&, const std::string&);

    SMOL_API void reg(u64_t type_id, load_fn_t load_fn, std::string_view display_name);
    SMOL_API asset_handle_t load(u64_t type_id, asset_registry_t& reg, const std::string& path);
    SMOL_API std::string_view display_name(u64_t type_id);
} // namespace smol::asset_serde
