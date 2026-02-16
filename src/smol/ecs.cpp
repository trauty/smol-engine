#include "ecs.h"
#include <atomic>
#include <memory>
#include <mutex>

namespace smol::ecs
{
    entity_t registry_t::create()
    {
        std::scoped_lock lock(recycle_mutex);
        if (!free_entities.empty())
        {
            entity_t entity = free_entities.back();
            free_entities.pop_back();
            return entity;
        }

        return entity_counter.fetch_add(1, std::memory_order_relaxed);
    }

    void registry_t::destroy(entity_t entity)
    {
        for (auto& [id, pool] : pools)
        {
            if (pool) { pool->remove(entity); }
        }

        std::scoped_lock lock(recycle_mutex);
        free_entities.push_back(entity);
    }
} // namespace smol::ecs