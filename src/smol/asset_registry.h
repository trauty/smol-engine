#pragma once

#include "smol/asset_handle.h"
#include "smol/asset_loader.h"
#include "smol/asset_meta.h"
#include "smol/asset_pool.h"
#include "smol/asset_types.h"
#include "smol/defines.h"
#include "smol/hash.h"
#include "smol/jobs.h"
#include "smol/log.h"

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <optional>
#include <utility>
#ifdef SMOL_ENABLE_PROFILING
    #include <common/TracySystem.hpp>
#endif
#include <mutex>
#include <string>
#include <unordered_map>

namespace smol
{
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

        std::string get_path(asset_handle_t handle)
        {
            if (!handle.is_valid()) { return {}; }

            std::scoped_lock lock(lookup_mutex);
            auto it = lookup.find(handle.uuid);
            if (it == lookup.end()) { return {}; }

            asset_pool_base_t* pool = get_pool_base(it->second.type_id);
            if (!pool || !pool->base_validate(handle.pool_index, handle.uuid)) { return {}; }

            return it->second.path;
        }

        void get_handles(u64_t type_id, std::vector<asset_handle_t>& out)
        {
            auto it = pools.find(type_id);
            if (it == pools.end()) { return; }
            it->second->base_get_handles(out);
        }

      private:
        std::unordered_map<u64_t, std::unique_ptr<asset_pool_base_t>> pools;
        std::mutex pools_mutex;

        asset_pool_base_t* get_pool_base(u64_t type_id)
        {
            auto it = pools.find(type_id);
            return (it != pools.end()) ? it->second.get() : nullptr;
        }

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
            uuid_t uuid = smol::asset_meta::resolve_uuid(path);
            std::string guid_str(smol::asset_meta::get_guid(path));

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
            slot->guid = guid_str;
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