#include "vfs.h"

#include "SDL3/SDL_iostream.h"
#include "SDL3/SDL_stdinc.h"
#include "smol/engine.h"

#include <SDL3/SDL_filesystem.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace smol::vfs
{
    namespace { std::unordered_map<std::string, std::string> mounts; }

    void init()
    {
        char* pref_path = SDL_GetPrefPath("smol-engine", smol::engine::get_game_name().c_str());
        if (pref_path)
        {
            mount("user://", pref_path);
            SDL_free(pref_path);
        }

#ifdef SMOL_PLATFORM_ANDROID
        mount("engine://assets/", "engine/");
        mount("game://assets/", "game/");
#else
        const char* base_path = SDL_GetBasePath();
        if (base_path)
        {
            std::string bin_path = base_path;

            mount("engine://assets/", bin_path + "assets/engine/");
            mount("game://assets/", bin_path + "assets/game/");
        }
#endif
    }

    void shutdown() { mounts.clear(); }

    void mount(const std::string& alias, const std::string& physical_path) { mounts[alias] = physical_path; }

    std::string resolve(std::string_view virtual_path)
    {
        std::string_view best_alias = "";
        std::string_view best_physical = "";

        for (const auto& pair : mounts)
        {
            if (virtual_path.starts_with(pair.first))
            {
                if (pair.first.length() > best_alias.length())
                {
                    best_alias = pair.first;
                    best_physical = pair.second;
                }
            }
        }

        if (!best_alias.empty())
        {
            std::string_view relative = virtual_path.substr(best_alias.length());

            if (!best_physical.empty() && best_physical.back() != '/' && best_physical.back() != '\\' &&
                !relative.empty() && relative.front() != '/' && relative.front() != '\\')
            {
                return std::string(best_physical) + "/" + std::string(relative);
            }

            return std::string(best_physical) + std::string(relative);
        }

        return std::string(virtual_path);
    }

    bool exists(const std::string& virtual_path)
    {
        SDL_IOStream* stream = open_read(virtual_path);
        if (stream)
        {
            SDL_CloseIO(stream);
            return true;
        }

        return false;
    }

    std::vector<u8_t> read_bytes(const std::string& virtual_path)
    {
        SDL_IOStream* stream = open_read(virtual_path);
        if (!stream) return {};

        i64_t size = SDL_GetIOSize(stream);
        if (size <= 0)
        {
            SDL_CloseIO(stream);
            return {};
        }

        std::vector<u8_t> buffer(size);
        SDL_ReadIO(stream, buffer.data(), size);
        SDL_CloseIO(stream);

        return buffer;
    }

    std::string read_text(const std::string& virtual_path)
    {
        std::vector<u8_t> bytes = read_bytes(virtual_path);
        return std::string(bytes.begin(), bytes.end());
    }

    SDL_IOStream* open_read(const std::string& virtual_path)
    {
        std::string physical_path = resolve(virtual_path);
        SDL_IOStream* stream = SDL_IOFromFile(physical_path.c_str(), "rb");
        return stream;
    }

    SDL_IOStream* open_write(const std::string& virtual_path)
    {
        std::string physical_path = resolve(virtual_path);
        SDL_IOStream* stream = SDL_IOFromFile(physical_path.c_str(), "wb");
        return stream;
    }
} // namespace smol::vfs