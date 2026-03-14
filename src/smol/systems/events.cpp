#include "events.h"

#include "smol/ecs.h"
#include "smol/ecs_fwd.h"
#include "smol/events.h"

#include <vector>

namespace smol::event_system
{
    void clear_frame_events(ecs::registry_t& reg)
    {
        auto view = reg.view<events::frame_event_tag>();
        reg.destroy(view.begin(), view.end());
    }
} // namespace smol::event_system