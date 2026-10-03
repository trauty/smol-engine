#pragma once

#include "tau/defines.h"

#include <SDL3/SDL_iostream.h>
#include <string>
#include <vector>

namespace tau::vfs
{
    TAU_ENGINE_API void init();
    TAU_ENGINE_API void shutdown();

    TAU_ENGINE_API void mount(const std::string& alias, const std::string& physical_path);

    // directory of the cooked tree init() found, parent of engine/ and game/, where guid_map.json sits. empty if none
    TAU_ENGINE_API const std::string& cooked_root();

    // splits "game://assets/x.png" into {"game://", "assets/x.png"}, no protocol gives {"", path}
    // everything that takes a vfs path apart goes through here instead of hunting for "://"
    struct path_parts_t
    {
        std::string_view protocol; // includes the "://", empty when there is none
        std::string_view rest;
    };

    TAU_ENGINE_API path_parts_t split_protocol(std::string_view virtual_path);

    TAU_ENGINE_API std::string resolve(std::string_view virtual_path);

    TAU_ENGINE_API bool exists(const std::string& virtual_path);

    TAU_ENGINE_API std::vector<u8_t> read_bytes(const std::string& virtual_path);
    TAU_ENGINE_API std::string read_text(const std::string& virtual_path);

    TAU_ENGINE_API SDL_IOStream* open_read(const std::string& virtual_path);
    TAU_ENGINE_API SDL_IOStream* open_write(const std::string& virtual_path);
} // namespace tau::vfs