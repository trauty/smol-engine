#include "asset_meta.h"

#include "smol/asset_table.h"
#include "smol/log.h"
#include "smol/vfs.h"

#include "json/json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>
#include <unordered_map>
#include <unordered_set>

namespace smol::asset_meta
{
    namespace
    {
        std::unordered_map<std::string, std::string> guid_map;
        std::unordered_map<std::string, std::string> reverse_guid_map;

        std::string strip_vfs_prefix(const std::string& path)
        {
            const smol::vfs::path_parts_t parts = smol::vfs::split_protocol(path);
            if (parts.protocol.empty()) { return path; }

            const std::size_t start = parts.rest.find('/');
            if (start == std::string_view::npos) { return path; }

            return std::string(parts.rest.substr(start + 1));
        }
    } // namespace

    void load_guid_map_json(const std::string& json_text)
    {
        auto data = nlohmann::json::parse(json_text, nullptr, false);
        if (data.is_discarded() || !data.is_object())
        {
            SMOL_LOG_ERROR("ASSET_META", "Failed to parse guid map data");
            return;
        }

        for (auto it = data.begin(); it != data.end(); ++it)
        {
            std::string path_key = it.key();
            std::string guid = it.value().get<std::string>();
            guid_map[path_key] = guid;
            reverse_guid_map[guid] = path_key;
        }
    }

    void load_guid_map(const std::string& guid_map_path)
    {
        std::string text = smol::vfs::read_text(guid_map_path);
        if (text.empty())
        {
            SMOL_LOG_INFO("ASSET_META", "No guid map found at {}", guid_map_path);
            return;
        }

        load_guid_map_json(text);
        SMOL_LOG_INFO("ASSET_META", "Loaded {} asset GUIDs from {}", guid_map.size(), guid_map_path);
    }

    void shutdown()
    {
        guid_map.clear();
        reverse_guid_map.clear();
    }

    std::vector<std::string> all_paths()
    {
        std::vector<std::string> paths;
        paths.reserve(guid_map.size());
        for (const auto& [path, guid] : guid_map) { paths.push_back(path); }

        std::sort(paths.begin(), paths.end());
        return paths;
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

        {
            static std::mutex warned_mutex;
            static std::unordered_set<std::string> warned;

            std::scoped_lock lock(warned_mutex);
            if (warned.insert(path).second)
            {
                // the path becomes the identity, so moving this asset breaks every reference that used its guid
                SMOL_LOG_ERROR("ASSET_META",
                               "No GUID for '{}' -- using its path as identity. References to it will "
                               "break if it moves. Cook the project so '{}.meta' is created and lands "
                               "in guid_map.json.",
                               path, path);
            }
        }

        return hash_string64(path);
    }

    std::string resolve_ref(std::string_view guid, const std::string& recorded_path)
    {
        if (guid.empty()) { return recorded_path; }

        const std::string_view current = get_path_for_guid(std::string(guid));
        if (!current.empty()) { return std::string(current); }

        // unknown guid: the asset was deleted or the map is stale, the recorded path is the only lead
        static std::mutex warned_mutex;
        static std::unordered_set<std::string> warned;
        {
            std::scoped_lock lock(warned_mutex);
            if (warned.insert(std::string(guid)).second)
            {
                SMOL_LOG_WARN("ASSET_META", "GUID {} is not in the map, falling back to its recorded path '{}'", guid,
                              recorded_path);
            }
        }

        return recorded_path;
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

    namespace
    {
        std::string guid_in_meta(const std::string& meta_path)
        {
            std::ifstream file(meta_path);
            if (!file.is_open()) { return {}; }

            const nlohmann::json meta = nlohmann::json::parse(file, nullptr, false);
            if (meta.is_discarded() || !meta.is_object()) { return {}; }
            return meta.value("guid", std::string{});
        }

        // false when the file is missing, mid edit or not a JSON object
        bool read_json_object(const std::string& path, nlohmann::json& out)
        {
            std::ifstream file(path);
            if (!file.is_open()) { return false; }

            out = nlohmann::json::parse(file, nullptr, false);
            return !out.is_discarded() && out.is_object();
        }

        // the file with a guid member inserted first, as text, so hand written materials keep key order,
        // compact arrays and line endings. empty when the text is not an object
        std::string with_guid_inserted(const std::string& text, const std::string& guid)
        {
            const std::size_t open = text.find('{');
            if (open == std::string::npos) { return {}; }

            const std::string nl = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";

            // indent like the member that follows, or four spaces if none
            const std::size_t first = text.find_first_not_of(" \t\r\n", open + 1);
            if (first == std::string::npos) { return {}; }
            const bool empty_object = text[first] == '}';

            std::string indent = "    ";
            const std::size_t line_start = text.find_last_of('\n', first);
            if (!empty_object && line_start != std::string::npos && line_start > open)
            {
                indent = text.substr(line_start + 1, first - line_start - 1);
            }

            const std::string member = nl + indent + "\"guid\": \"" + guid + "\"" + (empty_object ? "" : ",");
            return text.substr(0, open + 1) + member + (empty_object ? nl : "") + text.substr(open + 1);
        }

        bool keeps_guid_in_source(const std::string& path)
        {
            const asset_type_t* type = asset_table::by_source_extension(path);
            return type != nullptr && type->guid_in_source;
        }

        std::string find_or_create_meta_guid(const std::string& source_path)
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

        std::string find_or_create_source_guid(const std::string& source_path)
        {
            const std::string meta_path = source_path + ".meta";

            nlohmann::json doc;
            if (!read_json_object(source_path, doc))
            {
                // unreadable right now, likely half written: keep the identity in a .meta
                // the next cook of a readable file moves it inside
                return find_or_create_meta_guid(source_path);
            }

            std::string guid = doc.value("guid", std::string{});
            if (guid.empty())
            {
                guid = guid_in_meta(meta_path);
                const bool moved = !guid.empty();
                if (!moved) { guid = generate_uuid(); }

                std::string text;
                {
                    std::ifstream in(source_path, std::ios::binary);
                    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                }

                // checked before it replaces anything: the edit must still parse to the same document plus the guid
                const std::string edited = with_guid_inserted(text, guid);
                const nlohmann::json check = nlohmann::json::parse(edited, nullptr, false);
                doc["guid"] = guid;
                if (edited.empty() || check.is_discarded() || check != doc)
                {
                    SMOL_LOG_ERROR("ASSET_META", "Could not add a guid to {} without changing it, left as is",
                                   source_path);
                    return guid;
                }

                std::ofstream file(source_path, std::ios::binary | std::ios::trunc);
                file << edited;
                if (!file)
                {
                    SMOL_LOG_ERROR("ASSET_META", "Could not write the guid into {}", source_path);
                    return guid;
                }

                SMOL_LOG_INFO("ASSET_META", "{} the guid of {}", moved ? "Moved the .meta's" : "Wrote", source_path);
            }

            // the guid is inside now, a .meta beside it is redundant and a second place to disagree
            std::error_code ec;
            if (std::filesystem::remove(meta_path, ec))
            {
                SMOL_LOG_INFO("ASSET_META", "Removed {}, the guid lives in the file", meta_path);
            }

            return guid;
        }
    } // namespace

    std::string guid_for_writing(const std::string& source_path)
    {
        nlohmann::json doc;
        if (read_json_object(source_path, doc))
        {
            const std::string inside = doc.value("guid", std::string{});
            if (!inside.empty()) { return inside; }
        }

        const std::string sidecar = guid_in_meta(source_path + ".meta");
        return sidecar.empty() ? generate_uuid() : sidecar;
    }

    std::string find_or_create_guid(const std::string& source_path)
    {
        if (keeps_guid_in_source(source_path)) { return find_or_create_source_guid(source_path); }
        return find_or_create_meta_guid(source_path);
    }

    void write_guid_map(const std::string& output_path, const std::string& map_data_json,
                        const std::string& replace_prefix)
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

        if (!replace_prefix.empty() && merged.is_object())
        {
            for (auto it = merged.begin(); it != merged.end();)
            {
                if (it.key().rfind(replace_prefix, 0) == 0) { it = merged.erase(it); }
                else
                {
                    it++;
                }
            }
        }

        nlohmann::json incoming = nlohmann::json::parse(map_data_json, nullptr, false);
        if (incoming.is_object())
        {
            for (auto it = incoming.begin(); it != incoming.end(); it++) { merged[it.key()] = it.value(); }
        }

        std::string out_json = merged.dump(4);
        std::ofstream file(output_path);
        file << out_json;
        SMOL_LOG_INFO("ASSET_META", "Wrote guid map: {} ({} entries, {} bytes)", output_path, merged.size(),
                      out_json.size());
    }
} // namespace smol::asset_meta
