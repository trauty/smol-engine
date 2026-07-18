#include "asset_meta.h"

#include "smol/log.h"
#include "smol/vfs.h"

#include "json/json.hpp"
#include <filesystem>
#include <fstream>
#include <random>
#include <unordered_map>

namespace smol::asset_meta
{
    namespace
    {
        std::unordered_map<std::string, std::string> guid_map;
        std::unordered_map<std::string, std::string> reverse_guid_map;

        std::string strip_vfs_prefix(const std::string& path)
        {
            size_t proto = path.find("://");
            if (proto == std::string::npos) { return path; }
            size_t start = path.find('/', proto + 3);
            if (start == std::string::npos) { return path; }
            return path.substr(start + 1);
        }
    } // namespace

    void init(const std::string& guid_map_path)
    {
        std::string text = smol::vfs::read_text(guid_map_path);
        if (text.empty())
        {
            SMOL_LOG_INFO("ASSET_META", "No guid map found at {}", guid_map_path);
            return;
        }

        auto data = nlohmann::json::parse(text, nullptr, false);
        if (data.is_discarded())
        {
            SMOL_LOG_ERROR("ASSET_META", "Failed to parse guid map {}", guid_map_path);
            return;
        }

        for (auto it = data.begin(); it != data.end(); ++it)
        {
            std::string path_key = it.key();
            std::string guid = it.value().get<std::string>();
            guid_map[path_key] = guid;
            reverse_guid_map[guid] = path_key;
        }
        SMOL_LOG_INFO("ASSET_META", "Loaded {} asset GUIDs from {}", guid_map.size(), guid_map_path);
    }

    void shutdown()
    {
        guid_map.clear();
        reverse_guid_map.clear();
    }

    std::string_view get_guid(const std::string& path)
    {
        auto it = guid_map.find(path);
        if (it != guid_map.end()) { return it->second; }

        std::string stripped = strip_vfs_prefix(path);
        if (stripped != path)
        {
            it = guid_map.find(stripped);
            if (it != guid_map.end()) { return it->second; }
        }

        return {};
    }

    std::string_view get_path_for_guid(const std::string& guid)
    {
        auto it = reverse_guid_map.find(guid);
        if (it != reverse_guid_map.end()) { return it->second; }
        return {};
    }

    uuid_t resolve_uuid(const std::string& path)
    {
        std::string_view guid = get_guid(path);
        if (!guid.empty()) { return hash_string64(guid); }
        return hash_string64(path);
    }

    std::string generate_uuid()
    {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());

        union
        {
            struct
            {
                u64_t hi;
                u64_t lo;
            };
            u8_t bytes[16];
        } uuid;

        uuid.hi = gen();
        uuid.lo = gen();
        uuid.hi &= ~(u64_t(0xf000));
        uuid.hi |= u64_t(0x4000);
        uuid.bytes[8] = (uuid.bytes[8] & 0x3f) | 0x80;

        char buf[37];
        std::snprintf(buf, sizeof(buf), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                      uuid.bytes[0], uuid.bytes[1], uuid.bytes[2], uuid.bytes[3], uuid.bytes[4], uuid.bytes[5],
                      uuid.bytes[6], uuid.bytes[7], uuid.bytes[8], uuid.bytes[9], uuid.bytes[10], uuid.bytes[11],
                      uuid.bytes[12], uuid.bytes[13], uuid.bytes[14], uuid.bytes[15]);

        return std::string(buf, 36);
    }

    std::string find_or_create_guid(const std::string& source_path)
    {
        std::string meta_path = source_path + ".meta";

        nlohmann::json meta_json;
        if (std::filesystem::exists(meta_path))
        {
            std::ifstream file(meta_path);
            meta_json = nlohmann::json::parse(file, nullptr, false);
            if (!meta_json.is_discarded())
            {
                auto it = meta_json.find("guid");
                if (it != meta_json.end()) { return it->get<std::string>(); }
            }
        }

        std::string guid = generate_uuid();
        meta_json["guid"] = guid;

        std::ofstream file(meta_path);
        file << meta_json.dump(2);

        SMOL_LOG_INFO("ASSET_META", "Created meta file: {} → {}", meta_path, guid);
        return guid;
    }

    void write_guid_map(const std::string& output_path, const std::string& map_data_json)
    {
        std::filesystem::path out(output_path);
        std::filesystem::create_directories(out.parent_path());

        nlohmann::json merged;
        if (std::filesystem::exists(output_path))
        {
            std::ifstream existing(output_path);
            nlohmann::json prev = nlohmann::json::parse(existing, nullptr, false);
            if (prev.is_object()) { merged = std::move(prev); }
        }

        nlohmann::json incoming = nlohmann::json::parse(map_data_json, nullptr, false);
        if (incoming.is_object())
        {
            for (auto it = incoming.begin(); it != incoming.end(); ++it) { merged[it.key()] = it.value(); }
        }

        std::string out_json = merged.dump(4);
        std::ofstream file(output_path);
        file << out_json;
        SMOL_LOG_INFO("ASSET_META", "Wrote guid map: {} ({} entries, {} bytes)", output_path, merged.size(),
                      out_json.size());
    }
} // namespace smol::asset_meta
