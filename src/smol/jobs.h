#pragma once

#include "smol/defines.h"

#include <atomic>
#include <functional>

namespace smol::jobs
{
    struct counter_t : std::atomic<i32_t>
    {
        counter_t(i32_t v = 0) : std::atomic<i32_t>(v) {}
    };

    enum class priority_e
    {
        LOW, // io assets
        HIGH // everything else basically
    };

    void init();
    void shutdown();

    void kick(const std::function<void()>& task, counter_t* counter = nullptr, priority_e prio = priority_e::HIGH);
    void dispatch(u32_t count, u32_t batch_size, const std::function<void(u32_t, u32_t)>& task,
                  counter_t* counter = nullptr, priority_e priority = priority_e::HIGH);

    void wait(counter_t* counter);

    u32_t get_worker_count();
} // namespace smol::jobs