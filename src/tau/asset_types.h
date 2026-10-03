#pragma once

#include "tau/defines.h"

namespace tau
{
    using asset_id_t = u32_t;

    enum class asset_state_e
    {
        UNLOADED,
        QUEUED,
        READY,
        FAILED
    };
} // namespace tau