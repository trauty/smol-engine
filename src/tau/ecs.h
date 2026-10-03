#pragma once

#include "tau/defines.h"
#include "tau/ecs_fwd.h"

#include <entt/entt.hpp>

namespace tau::ecs
{
    TAU_ENGINE_API inline u32_t get_entity_id(entity_t entity) { return static_cast<u32_t>(entt::to_integral(entity)); }
} // namespace tau::ecs