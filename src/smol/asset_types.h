#pragma once

#include "smol/defines.h"

namespace smol
{
    using asset_id_t = u32_t;

    enum class asset_state_e
    {
        UNLOADED,
        QUEUED,
        READY,
        FAILED
    };
} // namespace smol