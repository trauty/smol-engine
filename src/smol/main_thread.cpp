#include "main_thread.h"

#include <mutex>
#include <queue>

namespace smol::main_thread
{
    namespace
    {
        std::mutex queue_mutex;
        std::queue<std::function<void()>> func_queue;
    } // namespace

    void enqueue(std::function<void()> func)
    {
        const std::lock_guard lock(queue_mutex);
        func_queue.push(func);
    }

    void execute()
    {
        const std::lock_guard lock(queue_mutex);
        while (!func_queue.empty())
        {
            func_queue.front()();
            func_queue.pop();
        }
    }
} // namespace smol::main_thread