#pragma once

#include "smol/ecs.h"

#include <utility>

namespace smol::events
{
    struct SMOL_API frame_event_tag
    {
    };

    template <typename T, typename... Args>
    SMOL_API void emit(ecs::registry_t& reg, Args&&... args)
    {
        ecs::entity_t entity = reg.create();
        reg.emplace<T>(entity, std::forward<Args>(args)...);
        reg.emplace<frame_event_tag>(entity);
    }
} // namespace smol::events