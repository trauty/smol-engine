#pragma once

#include "smol/asset.h"

namespace smol
{
    struct SMOL_API mesh_renderer_t
    {
        asset_handle_t mesh;
        asset_handle_t material;

        bool active = true;
    };
} // namespace smol