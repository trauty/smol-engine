#pragma once

#include "tau/defines.h"

#include <string>
#include <string_view>
#include <vector>

namespace tau
{
    // one description per kind of asset, read by the cooker, loader registration and the editor's picker
    struct asset_type_t
    {
        // stable name for tools that cannot see the engine's C++ asset types
        std::string_view key;

        u64_t type_id = 0;
        std::string_view display_name;

        // what the cooker turns this into, e.g. ".taumesh"
        std::string_view cooked_extension;

        // what a human authors, e.g. ".gltf" and ".glb". lowercase, leading dot.
        std::vector<std::string_view> source_extensions;

        // the source is JSON the engine writes itself, so its guid is a "guid" field inside it
        // foreign formats (images, glTF, shader source) cannot hold one and keep a .meta
        bool guid_in_source = false;
    };

    namespace asset_table
    {
        TAU_ENGINE_API const std::vector<asset_type_t>& all();

        // accepts either a bare extension or a whole path
        TAU_ENGINE_API const asset_type_t* by_source_extension(std::string_view path_or_ext);
        TAU_ENGINE_API const asset_type_t* by_type_id(u64_t type_id);
        TAU_ENGINE_API const asset_type_t* by_key(std::string_view key);

        // lowercased extension including the dot, or empty when the path has none
        TAU_ENGINE_API std::string extension_of(std::string_view path);

        // the cooked artifact a source path maps to, keeping the vfs protocol
        TAU_ENGINE_API std::string cooked_path(u64_t type_id, const std::string& source_path);
    } // namespace asset_table
} // namespace tau
