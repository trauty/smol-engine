#include "tau/asset_table.h"

#include "tau/asset_handle.h"
#include "tau/assets/material.h"
#include "tau/assets/mesh.h"
#include "tau/assets/scene.h"
#include "tau/assets/shader.h"
#include "tau/assets/texture.h"
#include "tau/hash.h"
#include "tau/vfs.h"

#include <filesystem>

#include <cctype>

namespace tau::asset_table
{
    const std::vector<asset_type_t>& all()
    {
        // the one place the engine's asset types are described. everything else asks.
        static const std::vector<asset_type_t> table = {
            {mesh_t::type_key, get_type_id<mesh_t>(), "Mesh", ".taumesh", {".gltf", ".glb"}},
            {material_t::type_key, get_type_id<material_t>(), "Material", ".taumat", {".mat"}, true},
            {texture_t::type_key, get_type_id<texture_t>(), "Texture", ".ktx2", {".png", ".jpg", ".jpeg"}},
            {shader_t::type_key, get_type_id<shader_t>(), "Shader", ".taushader", {".slang"}},
            {scene_t::type_key, get_type_id<scene_t>(), "Scene", ".tauscene", {".scene"}, true},
        };
        return table;
    }

    std::string extension_of(std::string_view path)
    {
        const std::size_t dot = path.find_last_of('.');
        if (dot == std::string_view::npos) { return {}; }

        // a dot in a directory name is not an extension
        const std::size_t slash = path.find_last_of("/\\");
        if (slash != std::string_view::npos && dot < slash) { return {}; }

        std::string ext(path.substr(dot));
        for (char& c : ext) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        return ext;
    }

    const asset_type_t* by_source_extension(std::string_view path_or_ext)
    {
        const std::string ext = extension_of(path_or_ext);
        if (ext.empty()) { return nullptr; }

        for (const asset_type_t& type : all())
        {
            for (std::string_view candidate : type.source_extensions)
            {
                if (candidate == ext) { return &type; }
            }
        }
        return nullptr;
    }

    const asset_type_t* by_type_id(u64_t type_id)
    {
        for (const asset_type_t& type : all())
        {
            if (type.type_id == type_id) { return &type; }
        }
        return nullptr;
    }

    const asset_type_t* by_key(std::string_view key)
    {
        for (const asset_type_t& type : all())
        {
            if (type.key == key) { return &type; }
        }
        return nullptr;
    }

    std::string cooked_path(u64_t type_id, const std::string& source_path)
    {
        const asset_type_t* type = by_type_id(type_id);
        if (type == nullptr) { return source_path; }

        const vfs::path_parts_t parts = vfs::split_protocol(source_path);

        std::filesystem::path cooked(parts.rest);
        cooked.replace_extension(std::string(type->cooked_extension));

        return std::string(parts.protocol) + cooked.generic_string();
    }
} // namespace tau::asset_table
