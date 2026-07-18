#pragma once

#include "smol/asset_loader.h"
#include "smol/assets/scene_format.h"
#include "smol/math.h"

#include <optional>
#include <string>
#include <vector>

namespace smol
{
    struct scene_property_t
    {
        u32_t prop_hash = 0;
        scene_value_type_e type = scene_value_type_e::I32;

        i32_t i = 0;
        u32_t u = 0;
        f32 f = 0.0f;
        bool b = false;
        vec3_t vec = {};
        std::string str;
        u64_t asset_type = 0;
    };

    struct scene_component_t
    {
        u32_t type_hash = 0;
        std::vector<scene_property_t> properties;
    };

    struct scene_entity_t
    {
        std::vector<scene_component_t> components;
    };

    struct scene_t
    {
        std::vector<scene_entity_t> entities;
    };

    template <>
    struct asset_loader_t<scene_t>
    {
        static std::optional<scene_t> load(const std::string& path);
    };
} // namespace smol
