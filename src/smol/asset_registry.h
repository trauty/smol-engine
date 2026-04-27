#pragma once

#include "smol/asset.h"
#include "smol/asset_loader.h"
#include "smol/asset_types.h"
#include "smol/defines.h"
#include "smol/hash.h"
#include "smol/jobs.h"
#include "smol/log.h"

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstring>
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
    template <typename T>
    concept has_asset_unload = requires(T& asset) {
        { asset_loader_t<T>::unload(asset) } -> std::same_as<void>;
    };

    struct SMOL_API asset_pool_base_t
    {
        virtual ~asset_pool_base_t() = default;
    };

    template <typename T>
    struct asset_pool_t : public asset_pool_base_t
    {
        struct slot_t
        {
            T data;
            asset_id_t id = 0;
            uuid_t uuid = 0;
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

        template <typename T, typename... Args>
        asset_handle_t load_async(const std::string& path, Args&&... args)
        { return internal_load<T>(path, false, std::forward<Args>(args)...); }

        template <typename T, typename... Args>
        asset_handle_t load_sync(const std::string& path, Args&&... args)
        { return internal_load<T>(path, true, std::forward<Args>(args)...); }

        template <typename T>
        T* get(asset_handle_t handle)
        {
            if (!handle.is_valid()) { return nullptr; }

            asset_pool_t<T>& pool = get_pool<T>();

            if (handle.pool_index >= pool.slots.size()) { return nullptr; }
            typename asset_pool_t<T>::slot_t& slot = pool.slots[handle.pool_index];
            if (slot.uuid != handle.uuid) { return nullptr; }

            if (slot.state.load(std::memory_order_acquire) == asset_state_e::READY) { return &slot.data; }

            return nullptr;
        }

        template <typename T>
        void release(asset_handle_t handle)
        {
            if (!handle.is_valid()) { return; }

            asset_pool_t<T>& pool = get_pool<T>();

            if (handle.pool_index >= pool.slots.size()) { return; }
            typename asset_pool_t<T>::slot_t& slot = pool.slots[handle.pool_index];
            if (slot.uuid != handle.uuid) { return; }

            if (slot.ref_count.fetch_sub(1) == 1)
            {
                if constexpr (has_asset_unload<T>) { asset_loader_t<T>::unload(slot.data); }

                slot.data = T();
                slot.uuid = 0;
                slot.state = asset_state_e::UNLOADED;

                std::scoped_lock pool_lock(pool.pool_mutex);
                pool.free_indices.push_back(slot.id);

                SMOL_LOG_INFO("ASSET", "Unloaded asset: {}", slot.path);

                std::scoped_lock map_lock(lookup_mutex);
                lookup.erase(handle.uuid);
            }
        }

      private:
        std::unordered_map<u64_t, std::unique_ptr<asset_pool_base_t>> pools;
        std::mutex pools_mutex;

        struct lookup_entry_t
        {
            void* slot_ptr;
            u64_t type_id;
            std::string path;
        };

        std::unordered_map<uuid_t, lookup_entry_t> lookup;
        std::mutex lookup_mutex;

        template <typename T>
        static u64_t get_asset_type_id()
        { return smol::get_type_id<T>(); }

        template <typename T>
        asset_pool_t<T>& get_pool()
        {
            static asset_pool_t<T>* cached_pool = nullptr;

            if (!cached_pool)
            {
                u64_t id = get_asset_type_id<T>();
                std::scoped_lock lock(pools_mutex);

                if (pools.find(id) == pools.end()) { pools[id] = std::make_unique<asset_pool_t<T>>(); }

                cached_pool = static_cast<asset_pool_t<T>*>(pools[id].get());
            }

            return *cached_pool;
        }

        template <typename T, typename... Args>
        asset_handle_t internal_load(const std::string& path, bool is_sync, Args&&... args)
        {
            uuid_t uuid = smol::hash_string(path.c_str());

            asset_pool_t<T>& pool = get_pool<T>();
            const size_t type_id = get_asset_type_id<T>();

            std::unique_lock map_lock(lookup_mutex);

            auto it = lookup.find(uuid);
            if (it != lookup.end() && it->second.type_id == type_id)
            {
                typename asset_pool_t<T>::slot_t* slot =
                    static_cast<typename asset_pool_t<T>::slot_t*>(it->second.slot_ptr);
                slot->ref_count.fetch_add(1);
                return asset_handle_t{uuid, slot->id};
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
            slot->uuid = uuid;
            slot->state = asset_state_e::QUEUED;
            slot->ref_count = 1;

            lookup[uuid] = {slot, type_id, path};

            map_lock.unlock();

            auto load_func = [slot, path, ... args = std::forward<Args>(args)]() mutable
            {
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
            };

            if (is_sync) { load_func(); }
            else
            {
                smol::jobs::kick_heavy(std::move(load_func), nullptr, jobs::priority_e::LOW);
            }

            return asset_handle_t{uuid, slot->id};
        }
    };

} // namespace smol