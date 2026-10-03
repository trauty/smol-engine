#include "events.h"

#include "tau/ecs.h"
#include "tau/ecs_fwd.h"
#include "tau/events.h"

#include <vector>

namespace tau::event_system
{
    void clear_frame_events(ecs::registry_t& reg)
    {
        auto view = reg.view<events::frame_event_tag>();
        reg.destroy(view.begin(), view.end());
    }
} // namespace tau::event_system