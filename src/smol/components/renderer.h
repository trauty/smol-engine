#pragma once

#include "smol/asset.h"
#include "smol/assets/mesh.h"
#include "smol/rendering/material.h"

namespace smol
{
    struct mesh_renderer_t
    {
        asset_t<mesh_t> mesh;
        material_t material;

        bool active = true;
    };
} // namespace smol