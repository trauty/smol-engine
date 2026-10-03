#pragma once

#include "tau/asset.h"
#include "tau/assets/material.h"
#include "tau/defines.h"
#include "tau/engine.h"

#include <vector>

namespace tau
{
    struct graphics_state_t
    {
        struct named_material_t
        {
            u32_t name_hash;
            asset_handle_t material;
        };

        std::vector<named_material_t> materials;

        asset_handle_t get_material(u32_t hash)
        {
            for (named_material_t& mat : materials)
            {
                if (mat.name_hash == hash) { return mat.material; }
            }

            return {};
        }

        material_t* get_material_raw(u32_t hash)
        {
            for (named_material_t& mat : materials)
            {
                if (mat.name_hash == hash) { return tau::engine::get_asset_registry().get<material_t>(mat.material); }
            }

            return nullptr;
        }

        void add_material(u32_t hash, asset_handle_t material) { materials.push_back({hash, material}); }
    };
} // namespace tau