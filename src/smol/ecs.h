#pragma once

#include "smol/defines.h"
#include "smol/ecs_fwd.h"

#include <entt/entt.hpp>

namespace smol::ecs
{
    SMOL_API inline u32_t get_entity_id(entity_t entity) { return static_cast<u32_t>(entt::to_integral(entity)); }
} // namespace smol::ecs