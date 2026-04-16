#include "material.h"

#include "smol/asset.h"
#include "smol/assets/shader.h"
#include "smol/log.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_resources.h"
#include "smol/rendering/renderer_types.h"

#include <optional>

namespace smol
{
    material_t::material_t(asset_t<shader_t> target_shader) : shader(target_shader)
    {
        for (u32_t i = 0; i < renderer::MAX_FRAMES_IN_FLIGHT; i++) { heap_offset[i] = renderer::BINDLESS_NULL_HANDLE; }

        if (!shader)
        {
            SMOL_LOG_ERROR("MATERIAL", "Cannot create material with null shader");
            return;
        }

        if (shader->has_material_data) { data.resize(shader->module.size, 0); }
        else
        {
            SMOL_LOG_WARN("SHADER", "Shader '{}' has no material info attached", shader->module.name);
        }
    }

    void material_t::sync()
    {
        if (dirty_frames == 0 || data.empty()) { return; }

        u32_t cur_frame = renderer::ctx.cur_frame;

        if (heap_offset[cur_frame] == renderer::BINDLESS_NULL_HANDLE)
        {
            heap_offset[cur_frame] = renderer::res_system.material_heap.allocate(data.size());
        }

        renderer::res_system.material_heap.update(heap_offset[cur_frame], data.data(), data.size());

        if (last_synced_frame != cur_frame)
        {
            dirty_frames--;
            last_synced_frame = cur_frame;
        }
    }

    std::optional<material_t> asset_loader_t<material_t>::load(const std::string& path, asset_t<shader_t> target_shader)
    {
        material_t mat(target_shader);
        if (!mat.shader) { return std::nullopt; }
        return mat;
    }

    void asset_loader_t<material_t>::unload(material_t& mat)
    {
        for (u32_t i = 0; i < renderer::MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (mat.heap_offset[i] != renderer::BINDLESS_NULL_HANDLE)
            {
                renderer::res_system.material_heap.free(mat.heap_offset[i], mat.data.size());
            }
        }

        mat.data.clear();
        mat.bound_textures.clear();
        mat.shader.release();
    }
}; // namespace smol