#include "asset_serde.h"

#include "smol/log.h"

namespace smol::asset_serde
{
    namespace
    {
        struct entry_t
        {
            load_fn_t load;
            std::string display_name;
        };

        std::unordered_map<u64_t, entry_t>& registry()
        {
            static std::unordered_map<u64_t, entry_t> reg;
            return reg;
        }
    } // namespace

    void reg(u64_t type_id, load_fn_t load_fn, std::string_view display_name)
    {
        registry()[type_id] = {load_fn, std::string(display_name)};
        SMOL_LOG_INFO("ASSET_SERDE", "Registered asset type '{}' (hash: {})", display_name, type_id);
    }

    asset_handle_t load(u64_t type_id, asset_registry_t& reg, const std::string& path)
    {
        auto it = registry().find(type_id);
        if (it == registry().end())
        {
            SMOL_LOG_ERROR("ASSET_SERDE", "No loader registered for type hash: {}", type_id);
            return {};
        }
        return it->second.load(reg, path);
    }

    std::string_view display_name(u64_t type_id)
    {
        auto it = registry().find(type_id);
        if (it == registry().end()) return "Unknown";
        return it->second.display_name;
    }
} // namespace smol::asset_serde
