#include "events.h"

#include <atomic>

namespace smol::events
{
    namespace detail
    {
        subscriber_map_t subscribers;
        std::atomic<size_t> handler_counter = 0;
    } // namespace detail

    subscriber_map_t& get_subscribers()
    {
        return detail::subscribers;
    }

    std::atomic<size_t>& get_counter()
    {
        return detail::handler_counter;
    }

    void unsubscribe(const subscription_id_t& sub_id)
    {
        subscriber_map_t::iterator it = detail::subscribers.find(sub_id.type);
        if (it != detail::subscribers.end())
        {
            it->second.erase(sub_id.handler_id);
        }
    }
} // namespace smol::events
