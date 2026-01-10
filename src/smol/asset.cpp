#include "asset.h"
#include "smol/log.h"
#include <functional>
#include <mutex>

namespace smol
{
    void asset_manager_t::init()
    {
        if (running) { return; }
        running = true;
        worker = std::thread(worker_func);
    }

    void asset_manager_t::shutdown()
    {
        {
            std::scoped_lock lock(queue_mutex);
            running = false;
        }
        queue_cv.notify_all();
        if (worker.joinable()) { worker.join(); }
    }

    void asset_manager_t::clear_all()
    {
        std::scoped_lock lock(registry_mutex);
        for (const std::function<void()>& clear_func : cleanup_registry) { clear_func(); }
        SMOL_LOG_INFO("ASSET", "All asset caches unloaded");
    }
} // namespace smol