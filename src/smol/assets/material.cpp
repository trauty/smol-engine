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

        for (shader_module_info_t& module : shader->modules)
        {
            if (module.name == shader_name)
            {
                shader_info = &module;
                break;
            }
        }

        data.resize(shader_info->size, 0);
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
        if (!mat.shader_info) { return std::nullopt; }
        return mat;
    }

    void asset_loader_t<material_t>::unload(material_t& mat)
    {
        mat.data.clear();
        mat.heap_offset = renderer::BINDLESS_NULL_HANDLE;
        mat.bound_textures.clear();
    }
}; // namespace smol