#pragma once

#include <functional>

namespace smol::main_thread
{
    void enqueue(std::function<void()> func);
    void execute();
} // namespace smol::main_thread