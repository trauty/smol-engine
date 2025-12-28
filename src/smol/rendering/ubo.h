#pragma once

#include "smol/defines.h"
#include "smol/main_thread.h"

#include <cstddef>
#include <glad/gl.h>
#include <memory>

namespace smol
{
    struct buffer_render_data_t
    {
        u32 id = 0;

        ~buffer_render_data_t()
        {
            u32 buf_id = id;
            if (buf_id != 0)
            {
                smol::main_thread::enqueue([buf_id]() { glDeleteBuffers(1, &buf_id); });
            }
        }
    };

    struct ubo_t
    {
        std::shared_ptr<buffer_render_data_t> buf_data = std::make_shared<buffer_render_data_t>();
        size_t size = 0;

        void init(size_t data_size);
        void update(const void* data, size_t data_size);
        void bind(u32 slot) const;

        bool ready() const { return buf_data->id != 0; }
    };
} // namespace smol