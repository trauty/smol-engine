#pragma once

#include "tau/ecs.h"

#include <utility>

namespace tau::events
{
    struct TAU_ENGINE_API frame_event_tag
    {
    };

    template <typename T, typename... Args>
    TAU_ENGINE_API void emit(ecs::registry_t& reg, Args&&... args)
    {
        ecs::entity_t entity = reg.create();
        reg.emplace<T>(entity, std::forward<Args>(args)...);
        reg.emplace<frame_event_tag>(entity);
    }
} // namespace tau::events