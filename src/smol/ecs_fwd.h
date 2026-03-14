#pragma once

#include "smol/defines.h"

#include <entt/entity/entity.hpp>
#include <entt/fwd.hpp>

namespace smol::ecs
{
    using entity_t = entt::entity;
    using registry_t = entt::registry;
    constexpr u32_t MAX_ENTITIES = 100000;
    constexpr entity_t NULL_ENTITY = entt::null;
} // namespace smol::ecs