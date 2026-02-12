#include "ecs.h"
#include <memory>

namespace smol::ecs
{
    entity_t registry_t::create()
    {
        if (!free_entities.empty())
        {
            entity_t entity = free_entities.back();
            free_entities.pop_back();
            return entity;
        }

        return entity_counter++;
    }

    void registry_t::destroy(entity_t entity)
    {
        for (std::unique_ptr<pool_t>& pool : pools)
        {
            if (pool) { pool->remove(entity); }
        }

        free_entities.push_back(entity);
    }
} // namespace smol::ecs