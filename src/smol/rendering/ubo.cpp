#include "ubo.h"
#include "smol/main_thread.h"
#include <SDL3/SDL_opengl.h>
#include <cstddef>

namespace smol
{
    void ubo_t::init(size_t data_size)
    {
        size = data_size;

        smol::main_thread::enqueue([data = buf_data, dat_size = size]() {
            glGenBuffers(1, &data->id);
            glBindBuffer(GL_UNIFORM_BUFFER, data->id);
            glBufferData(GL_UNIFORM_BUFFER, dat_size, nullptr, GL_DYNAMIC_DRAW);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        });
    }

    void ubo_t::update(const void* data, size_t data_size)
    {
        if (buf_data->id == 0) return;
        size_t actual_size = (data_size == 0) ? size : data_size;

        glBindBuffer(GL_UNIFORM_BUFFER, buf_data->id);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, actual_size, data);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void ubo_t::bind(u32 slot) const
    {
        if (buf_data->id != 0) { glBindBufferBase(GL_UNIFORM_BUFFER, slot, buf_data->id); }
    }
} // namespace smol