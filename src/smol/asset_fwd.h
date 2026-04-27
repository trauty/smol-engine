#pragma once

namespace smol
{
    template <typename T>
    struct asset_loader_t
    {
        // static std::optional<T> load(const std::string& path, Args...);
        // static void unload(T& asset);
    };

    class asset_registry_t;

    template <typename T>
    struct asset_pool_t;
} // namespace smol