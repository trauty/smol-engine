#pragma once

#include "smol/defines.h"

#include <cstddef>
#include <vector>

namespace smol
{
    struct SMOL_API linear_allocator_t
    {
        std::vector<u8_t> buffer;
        size_t cur_offset = 0;

        void init(size_t capacity);
        void* allocate(size_t size, size_t alignment = 16);
        void reset();

        size_t get_capacity() const { return buffer.size(); }
    };

    extern thread_local linear_allocator_t* active_arena;
} // namespace smol