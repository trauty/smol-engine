#pragma once

#include "smol/ecs.h"
#include <utility>

namespace smol::events
{
    struct frame_event_tag
    {
        SMOL_COMPONENT(frame_event_tag)
    };

    template<typename T, typename... Args>
    void emit(ecs::registry_t& reg, Args&&... args)
    {
        ecs::entity_t entity = reg.create();
        reg.emplace<T>(entity, std::forward<Args>(args)...);
        reg.emplace<frame_event_tag>(entity);
    }
} // namespace smol::events