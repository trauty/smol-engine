#include "asset_manager.h"

#include "mesh.h"
#include "shader.h"
#include "texture.h"

#include <thread>

using namespace smol::asset;

namespace smol::asset_manager
{
    namespace
    {
        std::unordered_map<std::string, std::shared_ptr<smol::asset::asset_t>> asset_cache;
        std::unordered_map<std::type_index, loader_entry_t> asset_loaders;

        std::mutex queue_mutex;
        std::queue<std::function<void()>> task_queue;
        std::condition_variable queue_cv;

        bool is_running = false;

        std::thread worker_thread;

        void worker_func()
        {
            while (is_running)
            {
                std::unique_lock lock(queue_mutex);
                queue_cv.wait(lock, []
                              { return !task_queue.empty() || !is_running; });

                if (!is_running && task_queue.empty())
                    break;

                std::function<void()> task = std::move(task_queue.front());
                task_queue.pop();
                lock.unlock();

                task();
            }
        }
    } // namespace

    namespace detail
    {
        std::unordered_map<std::string, std::shared_ptr<smol::asset::asset_t>>& get_asset_cache()
        {
            return asset_cache;
        }
        std::unordered_map<std::type_index, loader_entry_t>& get_asset_loaders()
        {
            return asset_loaders;
        }

        std::mutex& get_queue_mutex()
        {
            return queue_mutex;
        }
        std::queue<std::function<void()>>& get_task_queue()
        {
            return task_queue;
        }
        std::condition_variable& get_queue_cv()
        {
            return queue_cv;
        }
    } // namespace detail

    void init()
    {
        // asset handlers registrieren
        register_loader_with_args<texture_asset_t, texture_asset_args_t>();
        register_loader<shader_asset_t>();
        register_loader<mesh_asset_t>();

        is_running = true;

        worker_thread = std::thread(worker_func);
    }

    void shutdown()
    {
        {
            const std::lock_guard lock(queue_mutex);
            is_running = false;
        }
        queue_cv.notify_all();
        if (worker_thread.joinable())
        {
            worker_thread.join();
        }
    }

    void unload(const asset_handle_t handle)
    {
        const std::lock_guard lock(queue_mutex);
        asset_cache.erase(handle.get_id());
    }

    void unload(const std::string& id)
    {
        const std::lock_guard lock(queue_mutex);
        asset_cache.erase(id);
    }
} // namespace smol::asset_manager