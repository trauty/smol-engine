#pragma once

#include "smol/log.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace smol
{
    enum class asset_state_e
    {
        UNLOADED,
        LOADING,
        READY,
        ERROR
    };

    template<typename T>
    struct asset_slot_t
    {
        std::shared_ptr<T> data = nullptr;
        std::string path;
        std::atomic<asset_state_e> state = asset_state_e::UNLOADED;
        std::mutex data_mutex; // for hot reloading
    };

    template<typename T>
    class asset_t
    {
      public:
        asset_t() = default;
        asset_t(std::shared_ptr<asset_slot_t<T>> slot) : slot(slot) {}

        T* operator->() const { return get(); }
        T& operator*() const { return *get(); }

        T* get() const
        {
            if (!valid()) { return nullptr; }
            return slot->data.get();
        }

        bool valid() const { return slot && slot->state == asset_state_e::READY; }
        bool is_loading() const { return slot && slot->state == asset_state_e::LOADING; }
        const std::string& id() const { return slot ? slot->path : ""; }

        bool operator==(const asset_t& other) const { return slot == other.slot; }
        operator bool() const { return valid(); }

      private:
        std::shared_ptr<asset_slot_t<T>> slot;
    };

    template<typename T>
    struct asset_loader_t
    {
        // static std::optional<T> load(const std::string& path, Args...);
    };

    class asset_manager_t
    {
      public:
        static void init();
        static void shutdown();

        template<typename T, typename... Args>
        static asset_t<T> load(const std::string& path, Args&&... args)
        {
            std::shared_ptr<asset_slot_t<T>> slot = get_slot<T>(path);

            std::scoped_lock lock(slot->data_mutex);
            if (slot->state == asset_state_e::READY) { return asset_t<T>(slot); }

            slot->state = asset_state_e::LOADING;
            auto data = asset_loader_t<T>::load(path, std::forward<Args>(args)...);

            if (data)
            {
                slot->data = std::make_shared<T>(std::move(*data));
                slot->state = asset_state_e::READY;
            }
            else
            {
                slot->state = asset_state_e::ERROR;
                SMOL_LOG_ERROR("ASSET", "Failed to synchronously load asset '{}'", path);
            }

            return asset_t<T>(slot);
        }

        template<typename T, typename... Args>
        static asset_t<T> load_async(const std::string& path, std::function<void(asset_t<T>)> callback, Args&&... args)
        {
            std::shared_ptr<asset_slot_t<T>> slot = get_slot<T>(path);

            if (slot->state == asset_state_e::READY)
            {
                if (callback) { callback(asset_t<T>(slot)); }
                return asset_t<T>(slot);
            }

            {
                std::scoped_lock lock(queue_mutex);
                task_queue.push([slot, path, callback, args...]() mutable {
                    auto data = asset_loader_t<T>::load(args...);

                    {
                        std::scoped_lock lock(slot->data_mutex);
                        if (data)
                        {
                            slot->data = std::make_shared<T>(std::move(*data));
                            slot->state = asset_state_e::READY;
                        }
                        else
                        {
                            slot->state = asset_state_e::ERROR;
                            SMOL_LOG_ERROR("ASSET", "Failed to asynchronously load asset '{}'", path);
                        }
                    }

                    if (callback) { callback(asset_t<T>(slot)); }
                });
            }
            queue_cv.notify_one();

            return asset_t<T>(slot);
        }

        template<typename T, typename... Args>
        static void reload(const std::string& path, Args&&... args)
        {
            std::shared_ptr<asset_slot_t<T>> slot = get_slot<T>(path);

            auto new_data = asset_loader_t<T>::load(path, std::forward<Args>(args)...);
            if (new_data)
            {
                std::scoped_lock lock(slot->data_mutex);
                slot->data = std::make_shared<T>(std::move(*new_data));
                slot->state = asset_state_e::READY;
                SMOL_LOG_INFO("ASSET", "Hot-reloaded asset '{}'", path);
            }
        }

        template<typename T>
        static void unload(const std::string& path)
        {
            std::scoped_lock lock(storage_t<T>::mutex);
            storage_t<T>::cache.erase(path);
            SMOL_LOG_INFO("ASSET", "Unloaded asset: {}", path);
        }

        static void clear_all();

      private:
        template<typename T>
        struct storage_t
        {
            static inline std::unordered_map<std::string, std::shared_ptr<asset_slot_t<T>>> cache;
            static inline std::mutex mutex;
        };

        static inline std::vector<std::function<void()>> cleanup_registry;
        static inline std::mutex registry_mutex;

        template<typename T>
        static std::shared_ptr<asset_slot_t<T>> get_slot(const std::string& path)
        {
            static std::once_flag register_flag;
            std::call_once(register_flag, []() {
                std::scoped_lock lock(registry_mutex);
                cleanup_registry.push_back([]() {
                    std::scoped_lock cache_lock(storage_t<T>::mutex);
                    storage_t<T>::cache.clear();
                });
            });

            std::scoped_lock lock(storage_t<T>::mutex);
            auto& cache = storage_t<T>::cache;

            auto it = cache.find(path);
            if (it == cache.end())
            {
                std::shared_ptr<asset_slot_t<T>> slot = std::make_shared<asset_slot_t<T>>();
                slot->path = path;
                slot->state = asset_state_e::UNLOADED;
                cache[path] = slot;
                return slot;
            }

            return it->second;
        }

        static inline std::queue<std::function<void()>> task_queue;
        static inline std::mutex queue_mutex;
        static inline std::condition_variable queue_cv;
        static inline std::thread worker;
        static inline bool running = false;

        static void worker_func()
        {
            while (true)
            {
                std::function<void()> task;
                {
                    std::unique_lock lock(queue_mutex);
                    queue_cv.wait(lock, [] { return !task_queue.empty() || !running; });
                    if (!running && task_queue.empty()) break;

                    task = std::move(task_queue.front());
                    task_queue.pop();
                }
                task();
            }
        }
    };
} // namespace smol