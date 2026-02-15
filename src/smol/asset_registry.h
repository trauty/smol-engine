#pragma once

#include "smol/asset_types.h"
#include "smol/defines.h"
#include "smol/jobs.h"
#include "smol/log.h"

#include <atomic>
#include <cstddef>
#include <deque>
#include <optional>
#include <utility>
#include <vector>
#ifdef SMOL_ENABLE_PROFILING
#include <common/TracySystem.hpp>
#endif
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace smol
{
    template<typename T>
    struct asset_loader_t
    {
        // std::optional<T> load(const std::string& path, Args...);
    };

    struct asset_pool_base_t
    {
        virtual ~asset_pool_base_t() = default;
    };

    template<typename T>
    struct asset_pool_t : public asset_pool_base_t
    {
        struct slot_t
        {
            T data;
            asset_id_t id = 0;
            std::atomic<asset_state_e> state = asset_state_e::UNLOADED;
            std::atomic<i32_t> ref_count = 0;
            std::string path;
        };

        std::deque<slot_t> slots;
        std::vector<asset_id_t> free_indices;
        std::mutex pool_mutex;
    };

    class asset_registry_t
    {
      public:
        asset_registry_t() = default;
        ~asset_registry_t() { shutdown(); }

        void shutdown()
        {
            pools.clear();
            lookup.clear();
        }

        template<typename T, typename... Args>
        typename asset_pool_t<T>::slot_t* load(const std::string& path, Args&&... args)
        {
            asset_pool_t<T> pool = get_pool<T>();
            const size_t type_id = get_asset_type_id<T>();

            std::unique_lock map_lock(lookup_mutex);

            auto it = lookup.find(path);
            if (it != lookup.end() && it->second.type_id == type_id)
            {
                typename asset_pool_t<T>::slot_t* slot =
                    static_cast<typename asset_pool_t<T>::slot_t*>(it->second.slot_ptr);
                slot->ref_count.fetch_add(1);
                return slot;
            }

            typename asset_pool_t<T>::slot_t* slot = nullptr;
            {
                std::scoped_lock pool_lock(pool.pool_mutex);
                if (!pool.free_indices.empty())
                {
                    asset_id_t id = pool.free_indices.back();
                    pool.free_indices.pop_back();
                    slot = &pool.slots[id];
                    slot->data = T();
                }
                else
                {
                    pool.slots.emplace_back();
                    slot = &pool.slots.back();
                    slot->id = static_cast<asset_id_t>(pool.slots.size() - 1);
                }
            }

            slot->path = path;
            slot->state = asset_state_e::QUEUED;
            slot->ref_count = 1;

            lookup[path] = {slot, type_id};

            map_lock.unlock();

            smol::jobs::kick(
                [slot, path, ... args = std::forward<Args>(args)]() mutable {
                    std::optional<T> res = asset_loader_t<T>::load(path, args...);

                    if (res)
                    {
                        slot->data = std::move(*res);
                        slot->state = asset_state_e::READY;
                    }
                    else
                    {
                        slot->state = asset_state_e::FAILED;
                        SMOL_LOG_ERROR("ASSET", "Failed to load: {}", path);
                    }
                },
                nullptr, smol::jobs::priority_e::LOW);

            return slot;
        }

        template<typename T>
        void release(typename asset_pool_t<T>::slot_t* slot)
        {
            if (!slot) { return; }

            if (slot->ref_count.fetch_sub(1) == 1)
            {
                std::scoped_lock map_lock(lookup_mutex);

                if (slot->ref_count.load() > 0) { return; }

                lookup.erase(slot->path);

                slot->data = T();
                slot->state = asset_state_e::UNLOADED;

                asset_pool_t<T>& pool = get_pool<T>();
                std::scoped_lock pool_lock(pool.pool_mutex);
                pool.free_indices.push_back(slot->id);

                SMOL_LOG_INFO("ASSET", "Unloaded asset: {}", slot->path);
            }
        }

      private:
        std::vector<std::unique_ptr<asset_pool_base_t>> pools;

        struct lookup_entry_t
        {
            void* slot_ptr;
            size_t type_id;
        };

        std::unordered_map<std::string, lookup_entry_t> lookup;
        std::mutex lookup_mutex;

        static inline std::atomic<size_t> type_counter{0};
        template<typename T>
        static size_t get_asset_type_id()
        {
            static size_t id = type_counter++;
            return id;
        }

        template<typename T>
        asset_pool_t<T>& get_pool()
        {
            size_t id = get_asset_type_id<T>();

            if (id >= pools.size()) { pools.resize(id + 1); }
            if (!pools[id]) { pools[id] = std::make_unique<asset_pool_t<T>>(); }

            return static_cast<asset_pool_t<T>&>(*pools[id]);
        }
    };

} // namespace smol