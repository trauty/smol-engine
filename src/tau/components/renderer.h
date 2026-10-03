#pragma once

#include "tau/asset.h"

namespace tau
{
    struct TAU_ENGINE_API mesh_renderer_t
    {
        asset_handle_t mesh;
        asset_handle_t material;

        bool active = true;
        bool casts_shadow = true;
    };
} // namespace tau