#pragma once

#include "smol/asset.h"
#include "smol/assets/material.h"
#include "smol/assets/mesh.h"

namespace smol
{
    struct SMOL_API mesh_renderer_t
    {
        asset_t<mesh_t> mesh;
        asset_t<material_t> material;

        bool active = true;
    };
} // namespace smol