#pragma once

#include "smol/defines.h"

#include <SDL3/SDL_iostream.h>
#include <string>
#include <vector>

namespace smol::vfs
{
    SMOL_ENGINE_API void init();
    SMOL_ENGINE_API void shutdown();

    SMOL_ENGINE_API void mount(const std::string& alias, const std::string& physical_path);

    // Directory holding the cooked tree init() found: the parent of engine/ and game/,
    // and where guid_map.json sits. Empty if no cooked assets were located.
    SMOL_ENGINE_API const std::string& cooked_root();

    SMOL_ENGINE_API std::string resolve(std::string_view virtual_path);

    SMOL_ENGINE_API bool exists(const std::string& virtual_path);

    SMOL_ENGINE_API std::vector<u8_t> read_bytes(const std::string& virtual_path);
    SMOL_ENGINE_API std::string read_text(const std::string& virtual_path);

    SMOL_ENGINE_API SDL_IOStream* open_read(const std::string& virtual_path);
    SMOL_ENGINE_API SDL_IOStream* open_write(const std::string& virtual_path);
} // namespace smol::vfs