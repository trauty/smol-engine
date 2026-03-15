#pragma once

#include "smol/asset_registry.h"
#include "smol/ecs.h"
#include "smol/engine.h"
#include "smol/log.h"

#include <atomic>
#include <iostream>
#include <thread>

namespace smol
{
    template <typename T>
    struct asset_t
    {
        using slot_ptr = typename asset_pool_t<T>::slot_t*;

        slot_ptr slot = nullptr;
        asset_registry_t* registry = nullptr;

        asset_t() = default;
        asset_t(asset_registry_t* reg, slot_ptr slot) : registry(reg), slot(slot) {}

        asset_t(const asset_t& other) : slot(other.slot), registry(other.registry)
        {
            if (slot) { slot->ref_count.fetch_add(1); }
        }

        asset_t(asset_t&& other) noexcept : slot(other.slot), registry(other.registry)
        {
            other.slot = nullptr;
            other.registry = nullptr;
        }

        ~asset_t() { release(); }

        asset_t& operator=(const asset_t& other)
        {
            if (this != &other)
            {
                release();
                slot = other.slot;
                registry = other.registry;
                if (slot) { slot->ref_count.fetch_add(1); }
            }

            return *this;
        }

        asset_t& operator=(asset_t&& other) noexcept
        {
            if (this != &other)
            {
                release();
                slot = other.slot;
                registry = other.registry;

                other.slot = nullptr;
                other.registry = nullptr;
            }

            return *this;
        }

        void release()
        {
            if (slot && registry) { registry->release<T>(slot); }
            slot = nullptr;
            registry = nullptr;
        }

        T* get() const
        {
            if (slot && slot->state.load(std::memory_order_acquire) == asset_state_e::READY) { return &slot->data; }
            return nullptr;
        }

        T* operator->() const { return get(); }
        T& operator*() const { return *get(); }

        bool valid() const { return get() != nullptr; }
        operator bool() const { return valid(); }

        bool is_loading() const
        {
            if (!slot) { return false; }
            asset_state_e state = slot->state.load(std::memory_order_relaxed);
            return state == asset_state_e::LOADING || state == asset_state_e::QUEUED;
        }

        void wait() const
        {
            while (is_loading()) { std::this_thread::yield(); }
        }
    };

    template <typename T, typename... Args>
    asset_t<T> load_asset(const std::string& path, Args&&... args)
    {
        asset_registry_t& asset_reg = engine::get_asset_registry();
        typename asset_pool_t<T>::slot_t* slot = asset_reg.load_async<T>(path, std::forward<Args>(args)...);

        return asset_t<T>(&asset_reg, slot);
    }

    template <typename T, typename... Args>
    asset_t<T> load_asset_sync(const std::string& path, Args&&... args)
    {
        asset_registry_t& asset_reg = engine::get_asset_registry();
        typename asset_pool_t<T>::slot_t* slot = asset_reg.load_sync<T>(path, std::forward<Args>(args)...);

        return asset_t<T>(&asset_reg, slot);
    }
} // namespace smol