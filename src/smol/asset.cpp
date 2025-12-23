#include "asset.h"
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
} // namespace smol