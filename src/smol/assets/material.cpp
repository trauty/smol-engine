#include "material.h"

#include "smol/assets/shader.h"
#include "smol/log.h"
#include "smol/rendering/renderer_resources.h"
#include "smol/rendering/renderer_types.h"

#include <optional>

namespace smol
{
    void material_t::sync()
    {
        if (!is_dirty || data.empty()) { return; }

        if (heap_offset == renderer::BINDLESS_NULL_HANDLE)
        {
            heap_offset = renderer::res_system.material_heap.allocate(data.size());
        }

        renderer::res_system.material_heap.update(heap_offset, data.data(), data.size());

        is_dirty = false;
    }

    std::optional<material_t> asset_loader_t<material_t>::load(const std::string& path, asset_t<shader_t> shader)
    {
        shader.wait();

        if (!shader.valid())
        {
            SMOL_LOG_ERROR("MATERIAL", "Cannot create material '{}': Invalid shader", path);
            return std::nullopt;
        }

        material_t mat(shader);
        return mat;
    }

    void asset_loader_t<material_t>::unload(material_t& mat)
    {
        mat.data.clear();
        mat.heap_offset = renderer::BINDLESS_NULL_HANDLE;
        mat.bound_textures.clear();
    }
}; // namespace smol