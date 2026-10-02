#pragma once

#include "smol/defines.h"

#include <cstdint>
#include <string>

namespace smol
{
    using uuid_t = u64_t;

    struct SMOL_ENGINE_API asset_handle_t
    {
        uuid_t uuid = 0;
        u32_t pool_index = UINT32_MAX;

        bool is_valid() const { return uuid != 0; }
        bool operator==(const asset_handle_t& other) const { return uuid == other.uuid; }
        operator bool() const { return is_valid(); }
    };

} // namespace smol