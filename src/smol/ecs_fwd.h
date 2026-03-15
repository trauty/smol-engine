#pragma once

#include "smol/defines.h"

#include <entt/entity/entity.hpp>
#include <entt/fwd.hpp>

namespace smol::ecs
{
    using entity_t = SMOL_API entt::entity;
    using registry_t = SMOL_API entt::registry;
    SMOL_API constexpr u32_t MAX_ENTITIES = 100000;
    SMOL_API constexpr entity_t NULL_ENTITY = entt::null;
} // namespace smol::ecs