#pragma once

#include "defines.h"
#include "log.h"

#include <atomic>
#include <functional>
#include <stddef.h>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace smol::events
{
    struct event_t
    {
        event_t() = default;
        virtual ~event_t() = default;
    };

    struct subscription_id_t
    {
        std::type_index type;
        size_t handler_id;

        subscription_id_t() : type(typeid(void)), handler_id(0) {}
        subscription_id_t(std::type_index t, size_t h_id) : type(t), handler_id(h_id) {}

        bool operator==(const subscription_id_t& other) const
        {
            return type == other.type && handler_id == other.handler_id;
        }
    };

    using handler_fn_t = std::function<void(const event_t&)>;
    using handler_map_t = std::unordered_map<size_t, handler_fn_t>;
    using subscriber_map_t = std::unordered_map<std::type_index, handler_map_t>;

    subscriber_map_t& get_subscribers();
    std::atomic<size_t>& get_counter();

    template<typename EventT>
    subscription_id_t subscribe(std::function<void(const EventT&)> cb)
    {
        static_assert(std::is_base_of<event_t, EventT>::value, "Must inherit from event_t to register event");

        size_t id = get_counter().fetch_add(1);
        handler_map_t& handlers = get_subscribers()[typeid(EventT)];
        handlers[id] = [cb](const event_t& event)
        {
            cb(static_cast<const EventT&>(event));
        };

        return subscription_id_t {typeid(EventT), id};
    }

    void unsubscribe(const subscription_id_t& sub_id);

    template<typename EventT>
    void emit(const EventT& event)
    {
        static_assert(std::is_base_of<event_t, EventT>::value, "Must inherit from event_t to emit event");

        subscriber_map_t::const_iterator it = get_subscribers().find(typeid(EventT));
        if (it != get_subscribers().end())
        {
            for (auto& [_, handler] : it->second)
            {
                handler(event);
            }
        }
    }
} // namespace smol::events