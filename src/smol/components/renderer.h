#pragma once

#include "smol/asset.h"
#include "smol/assets/mesh.h"
#include "smol/ecs_types.h"
#include "smol/rendering/material.h"

namespace smol
{
    struct mesh_renderer_t
    {
        SMOL_COMPONENT(mesh_renderer_t);

        asset_t<mesh_t> mesh;
        material_t material;

        bool active = true;

        mesh_renderer_t() = default;

        mesh_renderer_t(mesh_renderer_t&&) = default;
        mesh_renderer_t& operator=(mesh_renderer_t&&) = default;

        mesh_renderer_t(const mesh_renderer_t&) = delete;
        mesh_renderer_t& operator=(const mesh_renderer_t&) = delete;
    };
} // namespace smol