#include "scene.h"

#include "smol/asset_handle.h"
#include "smol/assets/scene_format.h"
#include "smol/log.h"
#include "smol/vfs.h"

#include <SDL3/SDL_iostream.h>
#include <string>
#include <vector>

namespace smol
{
    namespace
    {
        template <typename T>
        bool read_pod(SDL_IOStream* stream, T& out)
        { return SDL_ReadIO(stream, &out, sizeof(T)) == sizeof(T); }

        bool read_str(SDL_IOStream* stream, std::string& out)
        {
            u32_t len = 0;
            if (!read_pod(stream, len)) { return false; }
            out.resize(len);
            return len == 0 || SDL_ReadIO(stream, out.data(), len) == len;
        }
    } // namespace

    std::optional<scene_t> asset_loader_t<scene_t>::load(const std::string& path)
    {
        std::string cooked_path = get_cooked_path(path, ".smolscene");

        SDL_IOStream* stream = smol::vfs::open_read(cooked_path);
        if (!stream)
        {
            SMOL_LOG_ERROR("SCENE", "Scene not found: {}", cooked_path);
            return std::nullopt;
        }

        scene_header_t header;
        if (!read_pod(stream, header) || header.magic != SMOL_SCENE_MAGIC)
        {
            SMOL_LOG_ERROR("SCENE", "Invalid .smolscene file: {}", cooked_path);
            SDL_CloseIO(stream);
            return std::nullopt;
        }

        if (header.version != SMOL_SCENE_VERSION)
        {
            SMOL_LOG_ERROR("SCENE", "Unsupported .smolscene version {} (engine expects {}), recook: {}", header.version,
                           SMOL_SCENE_VERSION, cooked_path);
            SDL_CloseIO(stream);
            return std::nullopt;
        }

        scene_t scene;
        scene.entities.reserve(header.entity_count);

        bool ok = true;
        for (u32_t e = 0; e < header.entity_count && ok; e++)
        {
            scene_entity_t& entity = scene.entities.emplace_back();

            u32_t component_count = 0;
            if (!read_pod(stream, component_count))
            {
                ok = false;
                break;
            }

            entity.components.reserve(component_count);
            for (u32_t c = 0; c < component_count && ok; c++)
            {
                scene_component_t& comp = entity.components.emplace_back();

                u32_t prop_count = 0;
                if (!read_pod(stream, comp.type_hash) || !read_pod(stream, prop_count))
                {
                    ok = false;
                    break;
                }

                comp.properties.reserve(prop_count);
                for (u32_t p = 0; p < prop_count && ok; p++)
                {
                    scene_property_t& prop = comp.properties.emplace_back();

                    u8_t type_raw = 0;
                    if (!read_pod(stream, prop.prop_hash) || !read_pod(stream, type_raw))
                    {
                        ok = false;
                        break;
                    }
                    prop.type = static_cast<scene_value_type_e>(type_raw);

                    switch (prop.type)
                    {
                    case scene_value_type_e::I32: ok = read_pod(stream, prop.i); break;
                    case scene_value_type_e::U32: ok = read_pod(stream, prop.u); break;
                    case scene_value_type_e::F32: ok = read_pod(stream, prop.f); break;
                    case scene_value_type_e::BOOL:
                    {
                        u8_t b = 0;
                        ok = read_pod(stream, b);
                        prop.b = b != 0;
                        break;
                    }
                    case scene_value_type_e::VEC3:
                        ok = read_pod(stream, prop.vec.x) && read_pod(stream, prop.vec.y) &&
                             read_pod(stream, prop.vec.z);
                        break;
                    case scene_value_type_e::STRING: ok = read_str(stream, prop.str); break;
                    case scene_value_type_e::ASSET_REF:
                        ok = read_pod(stream, prop.asset_type) && read_str(stream, prop.str);
                        break;
                    default: ok = false; break;
                    }
                }
            }
        }

        SDL_CloseIO(stream);

        if (!ok)
        {
            SMOL_LOG_ERROR("SCENE", "Truncated or corrupt .smolscene file: {}", cooked_path);
            return std::nullopt;
        }

        return scene;
    }
} // namespace smol
