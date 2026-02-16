#include "events.h"
#include "smol/ecs.h"
#include "smol/events.h"

#include <vector>

namespace smol::event_system
{
    void clear_frame_events(ecs::registry_t& reg)
    {
        std::vector<ecs::entity_t> to_destroy;
        for (auto [entity, event] : reg.view<events::frame_event_tag>()) { to_destroy.push_back(entity); }

        for (ecs::entity_t entity : to_destroy) { reg.destroy(entity); }
    }
} // namespace smol::event_system