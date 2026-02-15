#pragma once

namespace smol::ecs
{
    class registry_t;
}

namespace smol::event_system
{
    void clear_frame_events(ecs::registry_t& reg);
}