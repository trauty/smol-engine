#include "linear_allocator.h"

#include "tau/log.h"

namespace tau
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
            TAU_LOG_FATAL("MEMORY", "Linear allocator out of memory, capacity: {}", buffer.size());
            return nullptr;
        }

        void* ptr = buffer.data() + aligned_offset;
        cur_offset = aligned_offset + size;

        return ptr;
    }

    void linear_allocator_t::reset() { cur_offset = 0; }

    thread_local linear_allocator_t* active_arena = nullptr;

    linear_allocator_t* get_active_arena() { return active_arena; }
} // namespace tau