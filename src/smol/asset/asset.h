#pragma once

#include "smol/log.h"

#include <memory>
#include <mutex>
#include <string>

namespace smol::asset_manager
{
    struct asset_handle_t;
    void unload(const std::string& id);
} // namespace smol::asset_manager

namespace smol::asset
{
    class asset_t
    {
      public:
        virtual ~asset_t() = default; // für shared_ptr
    };

    template<typename T>
    class asset_ptr_t
    {
      public:
        asset_ptr_t(const std::shared_ptr<T>& ptr, const std::string& id) : ptr_to_asset(ptr), handle_id(id) {}
        asset_ptr_t(const asset_ptr_t& other) = default;
        asset_ptr_t() = default;
        asset_ptr_t& operator=(const asset_ptr_t& other) = default;

        ~asset_ptr_t()
        {
            if (ptr_to_asset.use_count() == 2)
            {
                smol::asset_manager::unload(handle_id);
                SMOL_LOG_DEBUG("ASSET_MANAGER", "Asset unloaded: {}", handle_id);
            }
        }

        T* operator->() const { return ptr_to_asset.get(); }
        T& operator*() const { return *ptr_to_asset; }
        explicit operator bool() const { return static_cast<bool>(ptr_to_asset); }
        bool operator==(std::nullptr_t) const { return ptr_to_asset == nullptr; }
        bool operator!=(std::nullptr_t) const { return ptr_to_asset != nullptr; }

        std::shared_ptr<T> get() const { return ptr_to_asset; }

      private:
        std::shared_ptr<T> ptr_to_asset;
        std::string handle_id;
    };
} // namespace smol::asset