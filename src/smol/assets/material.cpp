#include "material.h"

#include "smol/assets/shader.h"
#include "smol/log.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_resources.h"
#include "smol/rendering/renderer_types.h"

#include <optional>

namespace smol
{
    material_t::material_t(const std::string& shader_name)
    {
        auto it = renderer::ctx.shader_registry.find(shader_name);
        if (it == renderer::ctx.shader_registry.end())
        {
            SMOL_LOG_ERROR("MATERIAL", "Shader '{}' is not registered to any loaded uber shader", shader_name);
            return;
        }

        shader = it->second.shader;
        type_id = it->second.type_id;

        for (u32_t idx = 0; idx < shader->modules.size(); idx++)
        {
            if (shader->modules[idx].name == shader_name)
            {
                shader_module_idx = idx;
                break;
            }
        }

        if (shader_module_idx != NULL_SHADER_MODULE) { data.resize(shader->modules[shader_module_idx].size, 0); }
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

    std::optional<material_t> asset_loader_t<material_t>::load(const std::string& path, const std::string& shader_name)
    {
        material_t mat(shader_name);
        for (u32_t i = 0; i < renderer::MAX_FRAMES_IN_FLIGHT; i++)
        {
            mat.heap_offset[i] = renderer::BINDLESS_NULL_HANDLE;
        }
        if (mat.shader_module_idx == NULL_SHADER_MODULE) { return std::nullopt; }
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
        mat.shader_module_idx = NULL_SHADER_MODULE;
    }
}; // namespace smol