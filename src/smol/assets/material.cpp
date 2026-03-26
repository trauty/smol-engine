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
        if (!is_dirty || data.empty()) { return; }

        if (heap_offset == renderer::BINDLESS_NULL_HANDLE)
        {
            heap_offset = renderer::res_system.material_heap.allocate(data.size());
        }

        renderer::res_system.material_heap.update(heap_offset, data.data(), data.size());

        is_dirty = false;
    }

    std::optional<material_t> asset_loader_t<material_t>::load(const std::string& path, const std::string& shader_name)
    {
        material_t mat(shader_name);
        if (mat.shader_module_idx == NULL_SHADER_MODULE) { return std::nullopt; }
        return mat;
    }

    void asset_loader_t<material_t>::unload(material_t& mat)
    {
        if (mat.heap_offset != renderer::BINDLESS_NULL_HANDLE)
        {
            renderer::res_system.material_heap.free(mat.heap_offset, mat.data.size());
            mat.heap_offset = renderer::BINDLESS_NULL_HANDLE;
        }

        mat.data.clear();
        mat.bound_textures.clear();
        mat.shader_module_idx = NULL_SHADER_MODULE;
    }
}; // namespace smol