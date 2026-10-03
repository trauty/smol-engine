#include "asset_serde.h"

#include "tau/asset_table.h"
#include "tau/log.h"

#include <cctype>
#include <utility>

namespace tau::asset_serde
{
    namespace
    {
        struct entry_t
        {
            load_fn_t load;
            reload_fn_t reload;
        };

        std::unordered_map<u64_t, entry_t>& registry()
        {
            static std::unordered_map<u64_t, entry_t> reg;
            return reg;
        }
    } // namespace

    void reg(u64_t type_id, load_fn_t load_fn, reload_fn_t reload_fn)
    {
        registry()[type_id] = {load_fn, reload_fn};
        TAU_LOG_INFO("ASSET_SERDE", "Registered asset type '{}' (hash: {})", display_name(type_id), type_id);
    }

    asset_handle_t load(u64_t type_id, asset_registry_t& reg, const std::string& path)
    {
        auto it = registry().find(type_id);
        if (it == registry().end())
        {
            TAU_LOG_ERROR("ASSET_SERDE", "No loader registered for type hash: {}", type_id);
            return {};
        }
        return it->second.load(reg, path);
    }

    bool reload(u64_t type_id, asset_registry_t& reg, const std::string& path)
    {
        auto it = registry().find(type_id);
        if (it == registry().end() || it->second.reload == nullptr) { return false; }

        return it->second.reload(reg, path);
    }

    bool path_matches_type(u64_t type_id, std::string_view path)
    {
        const asset_type_t* type = asset_table::by_source_extension(path);
        return type != nullptr && type->type_id == type_id;
    }

    std::string_view display_name(u64_t type_id)
    {
        const asset_type_t* type = asset_table::by_type_id(type_id);
        return (type != nullptr) ? type->display_name : std::string_view{"Unknown"};
    }
} // namespace tau::asset_serde
