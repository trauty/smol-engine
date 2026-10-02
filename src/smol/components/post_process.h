#pragma once

#include "smol/asset.h"
#include "smol/defines.h"
#include "smol/math.h"

#include <vector>

namespace smol
{
    inline constexpr const char* DEFAULT_FINAL_PASS_PATH = "engine://assets/materials/uberpost.mat";
    inline constexpr u32_t POST_FINAL_PASS_SLOT = 0xFFFFFFFFu;

    struct SMOL_ENGINE_API post_process_t
    {
        std::vector<asset_handle_t> effects;
        asset_handle_t final_pass;
        bool enabled = true;

        void on_added();
    };

    struct SMOL_ENGINE_API post_volume_t
    {
        asset_handle_t overrides;
        bool is_global = false;
        vec3_t extents = {5.0f, 5.0f, 5.0f};
        f32 blend_distance = 2.0f;
        f32 weight = 1.0f;
        i32_t priority = 0;
    };
} // namespace smol
