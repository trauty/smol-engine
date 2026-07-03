#pragma once

#include "smol/asset_handle.h"
#include "smol/asset_loader.h"
#include "smol/asset_types.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace smol
{
    template <typename T>
    concept has_asset_unload = requires(T& asset) {
        { asset_loader_t<T>::unload(asset) } -> std::same_as<void>;
    };

    struct SMOL_API asset_pool_base_t
    {
        virtual ~asset_pool_base_t() = default;
        virtual void base_add_ref(u32_t index, uuid_t uuid) = 0;
        virtual bool base_release(u32_t index, uuid_t uuid) = 0;
        virtual std::string base_get_path(u32_t index, uuid_t uuid) = 0;
        virtual bool base_validate(u32_t index, uuid_t uuid) = 0;
        virtual void base_get_handles(std::vector<asset_handle_t>& out) = 0;
        virtual void base_unload_all() = 0;
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
            std::string guid;
            std::string path;
        };

        void base_add_ref(u32_t index, uuid_t uuid) override
        {
            if (index >= slots.size()) { return; }
            if (slots[index].uuid != uuid) { return; }
            slots[index].ref_count.fetch_add(1);
        }

        bool base_release(u32_t index, uuid_t uuid) override
        {
            if (index >= slots.size()) { return false; }
            if (slots[index].uuid != uuid) { return false; }
            return slots[index].ref_count.fetch_sub(1) == 1;
        }

        std::string base_get_path(u32_t index, uuid_t uuid) override
        {
            if (index >= slots.size()) { return {}; }
            if (slots[index].uuid != uuid) { return {}; }
            return slots[index].path;
        }

        bool base_validate(u32_t index, uuid_t uuid) override
        {
            if (index >= slots.size()) { return false; }
            return slots[index].uuid == uuid;
        }

        void base_get_handles(std::vector<asset_handle_t>& out) override
        {
            for (auto& slot : slots)
            {
                if (slot.state.load(std::memory_order_acquire) == asset_state_e::READY)
                {
                    out.push_back({slot.uuid, slot.id});
                }
            }
        }

        void base_unload_all() override
        {
            for (auto& slot : slots)
            {
                asset_state_e expected = asset_state_e::READY;
                if (slot.state.compare_exchange_strong(expected, asset_state_e::UNLOADED))
                {
                    if constexpr (has_asset_unload<T>) { asset_loader_t<T>::unload(slot.data); }
                    slot.data = T();
                    slot.uuid = 0;
                }
            }
        }

        std::deque<slot_t> slots;
        std::vector<asset_id_t> free_indices;
        std::mutex pool_mutex;
    };
} // namespace smol