#include "jobs.h"

#include "smol/defines.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace smol::jobs
{
    constexpr u32_t MAX_JOBS = 4096;
    constexpr u32_t MASK = MAX_JOBS - 1;

    struct job_t
    {
        job_function<64> task;
        counter_t* counter = nullptr;
    };

    struct job_queue_t
    {
        std::array<job_t, MAX_JOBS> buffer;
        std::atomic<u32_t> head{0};
        std::atomic<u32_t> tail{0};
        std::mutex wake_mutex;
        std::condition_variable wake_cv;

        bool push(job_function<64> task, counter_t* counter)
        {
            u32_t cur_tail = tail.fetch_add(1, std::memory_order_acq_rel);
            u32_t index = cur_tail & MASK;

            if (tail - head > MAX_JOBS) { return false; }

            buffer[index].task = task;
            buffer[index].counter = counter;

            return true;
        }

        bool pop(job_t& out_job)
        {
            u32_t cur_head = head.load(std::memory_order_acquire);
            while (true)
            {
                u32_t cur_tail = tail.load(std::memory_order_relaxed);
                if (cur_head == cur_tail) { return false; }

                if (head.compare_exchange_weak(cur_head, cur_head + 1, std::memory_order_release,
                                               std::memory_order_relaxed))
                {
                    u32_t index = cur_head & MASK;
                    out_job = std::move(buffer[index]);
                    return true;
                }
            }
        }
    };

    namespace
    {
        job_queue_t high_priority_queue;
        job_queue_t low_priority_queue;

        std::vector<std::thread> high_priority_workers;
        std::thread low_priority_worker; // one for the assets and general io
        std::atomic<bool> is_running{false};

        void worker_loop(job_queue_t& queue)
        {
            while (is_running.load(std::memory_order_relaxed))
            {
                job_t job;
                if (queue.pop(job))
                {
                    if (job.task) { job.task(); }

                    if (job.counter) { job.counter->fetch_sub(1, std::memory_order_release); }
                }
                else
                {
                    std::unique_lock lock(queue.wake_mutex);
                    queue.wake_cv.wait(lock);
                }
            }
        }
    } // namespace

    namespace detail
    {
        void push_job(job_function<64> task, counter_t* counter, priority_e prio)
        {
            if (prio == priority_e::HIGH) { high_priority_queue.push(std::move(task), counter); }
            else
            {
                low_priority_queue.push(std::move(task), counter);
            }
        }

        void wake_threads(priority_e prio, bool wake_all)
        {
            if (prio == priority_e::HIGH)
            {
                if (wake_all) high_priority_queue.wake_cv.notify_all();
                else high_priority_queue.wake_cv.notify_one();
            }
            else
            {
                if (wake_all) low_priority_queue.wake_cv.notify_all();
                else low_priority_queue.wake_cv.notify_one();
            }
        }
    } // namespace detail

    void init()
    {
        if (is_running) { return; }

        is_running = true;

        u32_t cores = std::thread::hardware_concurrency();
        u32_t num_high = std::max(1u, cores - 1);

        for (u32_t i = 0; i < num_high; i++)
        {
            high_priority_workers.emplace_back(worker_loop, std::ref(high_priority_queue));
        }

        low_priority_worker = std::thread(worker_loop, std::ref(low_priority_queue));
    }

    void shutdown()
    {
        is_running = false;

        high_priority_queue.wake_cv.notify_all();
        low_priority_queue.wake_cv.notify_all();

        for (std::thread& worker : high_priority_workers)
        {
            if (worker.joinable()) { worker.join(); }
        }
        high_priority_workers.clear();

        if (low_priority_worker.joinable()) { low_priority_worker.join(); }
    }

    // job stealing for main thread
    void wait(counter_t* counter)
    {
        if (!counter) { return; }

        while (counter->load(std::memory_order_acquire) > 0)
        {
            job_t job;
            if (high_priority_queue.pop(job))
            {
                if (job.task) { job.task(); }
                if (job.counter) { job.counter->fetch_sub(1, std::memory_order_release); }
            }
            else
            {
                std::this_thread::yield();
            }
        }

        counter->store(0, std::memory_order_relaxed);
    }

    u32_t get_worker_count() { return static_cast<u32_t>(high_priority_workers.size()); }
} // namespace smol::jobs