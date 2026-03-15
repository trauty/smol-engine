#include "material.h"

#include "smol/assets/shader.h"
#include "smol/log.h"
#include "smol/rendering/renderer_types.h"

#include <optional>

namespace smol
{
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