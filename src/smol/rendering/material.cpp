#include "material.h"

#include "smol/asset/shader.h"
#include "smol/defines.h"
#include <cstring>

namespace smol
{
    void material_t::init(const smol::shader_asset_t& s)
    {
        shader = s;

        if (shader.shader_data->reflection.ubo_sizes.count(1))
        {
            size_t size = shader.shader_data->reflection.ubo_sizes[1];
            parameter_buf.resize(size);
            std::memset(parameter_buf.data(), 0, size);

            ubo.init(size);
        }
    }
} // namespace smol