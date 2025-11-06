#pragma once

#include "asset.h"
#include "smol/log.h"
#include "smol/util.h"

#include <any>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace smol::asset_manager
{
    struct asset_handle_t
    {
        std::string id;

        explicit asset_handle_t(std::string_view new_id) : id(std::move(new_id)) {}
        const std::string& get_id() const { return id; }
        bool valid() const { return !id.empty(); }
    };

    struct loader_entry_t
    {
        std::function<std::shared_ptr<smol::asset::asset_t>(const std::string&, const std::any&)> loader;
        std::type_index arg_type;

        loader_entry_t(
            std::function<std::shared_ptr<smol::asset::asset_t>(const std::string&, const std::any&)> loader_func,
            std::type_index arg_type_index) : loader(std::move(loader_func)),
                                              arg_type(arg_type_index) {}
    };

    namespace detail
    {
        using loader_func_t = std::function<std::shared_ptr<smol::asset::asset_t>(const std::string& path, const std::any& args)>;
        using cache_iter_t = std::unordered_map<std::string, std::shared_ptr<smol::asset::asset_t>>::const_iterator;
        using loaders_iter_t = std::unordered_map<std::type_index, loader_entry_t>::const_iterator;

        std::unordered_map<std::string, std::shared_ptr<smol::asset::asset_t>>& get_asset_cache();
        std::unordered_map<std::type_index, loader_entry_t>& get_asset_loaders();

        std::mutex& get_queue_mutex();
        std::queue<std::function<void()>>& get_task_queue();
        std::condition_variable& get_queue_cv();
    } // namespace detail

    using namespace detail;

    void init();
    void shutdown();

    template<typename T>
    void register_loader()
    {
        static_assert(std::is_base_of_v<smol::asset::asset_t, T>, "Asset type must derive from asset_t");

        get_asset_loaders().insert_or_assign(typeid(T), loader_entry_t {[](const std::string& path, const std::any& _) -> std::shared_ptr<smol::asset::asset_t>
                                                                        {
                                                                            return std::make_shared<T>(path);
                                                                        },
                                                                        typeid(std::any)});
    }

    template<typename T, typename Args>
    void register_loader_with_args()
    {
        static_assert(std::is_base_of_v<smol::asset::asset_t, T>, "Asset type must derive from asset_t");

        get_asset_loaders().insert_or_assign(typeid(T), loader_entry_t {[](const std::string& path, const std::any& a) -> std::shared_ptr<smol::asset::asset_t>
                                                                        {
                                                                            const Args& args = std::any_cast<const Args&>(a);
                                                                            return std::make_shared<T>(path, args);
                                                                        },
                                                                        typeid(Args)});
    }

    template<typename T, typename Args>
    asset_handle_t load_sync(const std::string& path, const Args& args)
    {
        const std::lock_guard lock(get_queue_mutex());

        size_t delim = path.find('|');
        if (delim == std::string::npos)
        {
            if (!std::filesystem::exists(path))
            {
                SMOL_LOG_ERROR("ASSET_MANAGER", "Asset at '{}' doesn't exist", path);
                return asset_handle_t {""};
            }
        }

        std::unordered_map<std::string, std::shared_ptr<smol::asset::asset_t>>& asset_cache = detail::get_asset_cache();

        if (cache_iter_t it = asset_cache.find(path); it != asset_cache.end())
        {
            if (it->second != nullptr)
            {
                return asset_handle_t {path};
            }
        }

        const std::unordered_map<std::type_index, loader_entry_t>& asset_loaders = get_asset_loaders();

        loaders_iter_t loader_it = asset_loaders.find(typeid(T));
        if (loader_it == asset_loaders.end())
        {
            SMOL_LOG_ERROR("ASSET_MANAGER", "Loader for asset on path '{}' isn't registered or doesn't exist", path);
            return asset_handle_t {""};
        }

        if (loader_it->second.arg_type != typeid(Args))
        {
            SMOL_LOG_ERROR("ASSET_MANAGER", "Loader for '{}' expects argument type '{}', but got '{}'", path, loader_it->second.arg_type.name(), typeid(Args).name());
            return asset_handle_t {""};
        }

        std::string cache_key = path;
        if constexpr (smol::util::hashable_t<Args>)
        {
            std::size_t hash = std::hash<Args> {}(*args);
            cache_key = path + "#" + std::to_string(hash);
        }

        std::shared_ptr<smol::asset::asset_t> asset_ptr = loader_it->second.loader(
            path,
            args);
        asset_cache[cache_key] = asset_ptr;

        return asset_handle_t {cache_key};
    }

    template<typename T>
    asset_handle_t load_sync(const std::string& path)
    {
        return load_sync<T>(path, std::any {});
    }

    // dafür wäre mal ein job system ziemlich cool, macht aber fürs erste nur ein thread
    template<typename T, typename Args>
    void load_async(const std::string& path, const Args& args, std::function<void(asset_handle_t)> callback)
    {
        std::function<void()> task = [path, args, callback = callback]
        {
            callback(load_sync<T>(path, args));
        };

        {
            const std::lock_guard lock(get_queue_mutex());
            get_task_queue().push(std::move(task));
        }

        get_queue_cv().notify_one();
    }

    template<typename T>
    void load_async(const std::string& path, std::function<void(asset_handle_t)> callback)
    {
        load_async<T>(path, std::any {}, callback);
    }

    template<typename T>
    asset::asset_ptr_t<T> get(const asset_handle_t& handle)
    {
        const std::lock_guard lock(get_queue_mutex());

        if (cache_iter_t it = get_asset_cache().find(handle.get_id()); it != get_asset_cache().end())
        {
            return asset::asset_ptr_t<T>(dynamic_pointer_cast<T>(it->second), handle.get_id());
        }

        SMOL_LOG_WARN("ASSET_MANAGER", "Handle with ID '{}' expired", handle.get_id());

        return asset::asset_ptr_t<T> {};
    }

    void unload(const asset_handle_t handle);
    void unload(const std::string& id);
} // namespace smol::asset_manager