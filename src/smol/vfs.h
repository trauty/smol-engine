#pragma once

#include "smol/defines.h"

#include <SDL3/SDL_iostream.h>
#include <string>
#include <vector>

namespace smol::vfs
{
    void init();
    void shutdown();

    void mount(const std::string& alias, const std::string& physical_path);

    std::string resolve(std::string_view virtual_path);

    bool exists(const std::string& virtual_path);

    std::vector<u8_t> read_bytes(const std::string& virtual_path);
    std::string read_text(const std::string& virtual_path);

    SDL_IOStream* open_read(const std::string& virtual_path);
    SDL_IOStream* open_write(const std::string& virtual_path);
} // namespace smol::vfs