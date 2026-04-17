#include "linear_allocator.h"

#include "smol/log.h"

namespace smol
{
    void linear_allocator_t::init(size_t capacity)
    {
        buffer.resize(capacity);
        cur_offset = 0;
    }

    void* linear_allocator_t::allocate(size_t size, size_t alignment)
    {
        size_t aligned_offset = (cur_offset + alignment - 1) & ~(alignment - 1);

        if (aligned_offset + size > buffer.size())
        {
            SMOL_LOG_FATAL("MEMORY", "Linear allocator out of memory, capacity: {}", buffer.size());
            return nullptr;
        }

        void* ptr = buffer.data() + aligned_offset;
        cur_offset = aligned_offset + size;

        return ptr;
    }

    void linear_allocator_t::reset() { cur_offset = 0; }

    thread_local linear_allocator_t* active_arena = nullptr;
} // namespace smol