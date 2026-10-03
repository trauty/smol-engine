#include "serialization.h"

#include "entt/meta/meta.hpp"
#include "entt/meta/resolve.hpp"
#include "tau/asset.h"
#include "tau/asset_meta.h"
#include "tau/asset_serde.h"
#include "tau/assets/scene_format.h"
#include "tau/ecs_fwd.h"
#include "tau/engine.h"
#include "tau/hash.h"
#include "tau/log.h"
#include "tau/math.h"
#include "tau/reflection.h"

#include "json/json.hpp"
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tau::serialization
{
    namespace
    {
        nlohmann::json tagged(scene_value_type_e type, nlohmann::json value)
        {
            return {
                {"ty", static_cast<u8_t>(type)},
                {"v",  std::move(value)       }
            };
        }

        std::string meta_key(const char* name, u32_t id) { return name ? std::string(name) : std::to_string(id); }

        bool is_numeric_key(const std::string& key)
        { return !key.empty() && key.find_first_not_of("0123456789") == std::string::npos; }

        u32_t key_to_hash(const std::string& key)
        { return is_numeric_key(key) ? static_cast<u32_t>(std::stoul(key)) : tau::hash_string(key); }

        std::string describe_key(const std::string& name, u32_t hash)
        { return name.empty() ? std::to_string(hash) : name; }

        template <typename T>
        void write_pod(std::ofstream& out, const T& v)
        { out.write(reinterpret_cast<const char*>(&v), sizeof(T)); }

        void write_str(std::ofstream& out, const std::string& s)
        {
            u32_t len = static_cast<u32_t>(s.size());
            write_pod(out, len);
            out.write(s.data(), s.size());
        }

        std::unordered_map<u32_t, tau::reflection::type_t> component_types_by_pool(tau::world_t& world)
        {
            std::unordered_map<u32_t, tau::reflection::type_t> by_pool;
            for (auto [meta_id, type] : tau::reflection::resolve(*world.reflection_ctx))
            {
                if (!type.func("get"_h)) { continue; } // not a component
                by_pool.emplace(static_cast<u32_t>(type.info().hash()), type);
            }
            return by_pool;
        }

        nlohmann::json encode_component_fields(tau::world_t& world, tau::reflection::type_t type,
                                               tau::reflection::any_t& instance)
        {
            nlohmann::json comp_json = nlohmann::json::object();

            for (auto [data_id, data] : type.data())
            {
                tau::reflection::type_t field_type = data.type();
                tau::reflection::any_t field_value = data.get(instance);
                std::string prop_key = meta_key(data.name(), data_id);

                tau::reflection::editor_prop_t* prop = static_cast<tau::reflection::editor_prop_t*>(data.custom());
                if (prop && prop->asset_type_hash != 0 && prop->is_list)
                {
                    const std::vector<asset_handle_t> list = field_value.cast<std::vector<asset_handle_t>>();

                    nlohmann::json entries = nlohmann::json::array();
                    for (const asset_handle_t& handle : list)
                    {
                        std::string path = tau::engine::get_asset_registry().get_path(handle);
                        std::string_view guid = tau::asset_meta::get_guid(path);
                        entries.push_back({
                            {"g", guid.empty() ? "" : std::string(guid)},
                            {"p", path                                 }
                        });
                    }

                    comp_json[prop_key] = tagged(scene_value_type_e::ASSET_REF_LIST,
                                                 {
                                                     {"t", prop->asset_type_hash},
                                                     {"e", std::move(entries)   }
                    });
                    continue;
                }

                if (prop && prop->asset_type_hash != 0)
                {
                    asset_handle_t handle = field_value.cast<asset_handle_t>();
                    std::string path = tau::engine::get_asset_registry().get_path(handle);
                    std::string_view guid = tau::asset_meta::get_guid(path);
                    comp_json[prop_key] = tagged(
                        scene_value_type_e::ASSET_REF,
                        {
                            {"t", prop->asset_type_hash                },
                            {"g", guid.empty() ? "" : std::string(guid)},
                            {"p", path                                 }
                    });
                    continue;
                }

                if (field_type == tau::reflection::resolve<i32>(*world.reflection_ctx))
                {
                    comp_json[prop_key] = tagged(scene_value_type_e::I32, field_value.cast<i32>());
                }
                else if (field_type == tau::reflection::resolve<u32>(*world.reflection_ctx))
                {
                    comp_json[prop_key] = tagged(scene_value_type_e::U32, field_value.cast<u32>());
                }
                else if (field_type == tau::reflection::resolve<f32>(*world.reflection_ctx))
                {
                    comp_json[prop_key] = tagged(scene_value_type_e::F32, field_value.cast<f32>());
                }
                else if (field_type == tau::reflection::resolve<bool>(*world.reflection_ctx))
                {
                    comp_json[prop_key] = tagged(scene_value_type_e::BOOL, field_value.cast<bool>());
                }
                else if (field_type == tau::reflection::resolve<std::string>(*world.reflection_ctx))
                {
                    comp_json[prop_key] = tagged(scene_value_type_e::STRING, field_value.cast<std::string>());
                }
                else if (field_type == tau::reflection::resolve<vec3_t>(*world.reflection_ctx))
                {
                    vec3_t vec = field_value.cast<tau::vec3_t>();
                    comp_json[prop_key] = tagged(scene_value_type_e::VEC3, {vec.x, vec.y, vec.z});
                }
                else if (field_type.is_enum())
                {
                    const tau::reflection::any_t as_int = std::as_const(field_value).allow_cast<i32>();
                    if (as_int) { comp_json[prop_key] = tagged(scene_value_type_e::I32, as_int.cast<i32>()); }
                }
                else
                {
                    TAU_LOG_WARN("SCENE", "Field '{}' has unsupported type '{}' -- not saved", prop_key,
                                 field_type.name() ? field_type.name() : "?");
                }
            }

            return comp_json;
        }
    } // namespace

    nlohmann::json serialize_scene(tau::world_t& world)
    {
        nlohmann::json scene_data;
        scene_data["entities"] = nlohmann::json::array();

        const std::unordered_map<u32_t, tau::reflection::type_t> by_pool = component_types_by_pool(world);

        std::unordered_map<tau::ecs::entity_t, std::size_t> slots;
        for (tau::ecs::entity_t entity : world.registry.view<tau::ecs::entity_t>())
        {
            slots.emplace(entity, scene_data["entities"].size());

            nlohmann::json entity_json;
            entity_json["components"] = nlohmann::json::object();
            scene_data["entities"].push_back(std::move(entity_json));
        }

        for (auto [pool_id, pool] : world.registry.storage())
        {
            const auto type_it = by_pool.find(static_cast<u32_t>(pool_id));
            if (type_it == by_pool.end()) { continue; }

            const tau::reflection::type_t type = type_it->second;
            const tau::reflection::func_t get_func = type.func("get"_h);
            const std::string type_key = meta_key(type.name(), static_cast<u32_t>(type.id()));

            for (tau::ecs::entity_t entity : pool)
            {
                const auto slot_it = slots.find(entity);
                if (slot_it == slots.end()) { continue; }

                tau::reflection::any_t instance =
                    get_func.invoke({}, tau::reflection::forward_as_meta(world.registry), entity);
                if (!instance) { continue; }

                scene_data["entities"][slot_it->second]["components"][type_key] =
                    encode_component_fields(world, type, instance);
            }
        }

        return scene_data;
    }

    scene_t scene_from_json(const nlohmann::json& scene_data)
    {
        scene_t scene;

        if (!scene_data.contains("entities")) { return scene; }

        for (const nlohmann::json& entity_json : scene_data["entities"])
        {
            scene_entity_t& out_entity = scene.entities.emplace_back();

            for (const auto& [type_key, props_json] : entity_json["components"].items())
            {
                scene_component_t& out_comp = out_entity.components.emplace_back();
                out_comp.type_hash = key_to_hash(type_key);
                if (!is_numeric_key(type_key)) { out_comp.type_name = type_key; }

                for (const auto& [prop_key, val] : props_json.items())
                {
                    scene_property_t prop;
                    prop.prop_hash = key_to_hash(prop_key);
                    if (!is_numeric_key(prop_key)) { prop.prop_name = prop_key; }

                    if (val.is_object() && val.contains("ty") && val.contains("v"))
                    {
                        prop.type = static_cast<scene_value_type_e>(val["ty"].get<u8_t>());
                        const nlohmann::json& v = val["v"];
                        switch (prop.type)
                        {
                        case scene_value_type_e::I32: prop.i = v.get<i32_t>(); break;
                        case scene_value_type_e::U32: prop.u = v.get<u32_t>(); break;
                        case scene_value_type_e::F32: prop.f = v.get<f32>(); break;
                        case scene_value_type_e::BOOL: prop.b = v.get<bool>(); break;
                        case scene_value_type_e::STRING: prop.str = v.get<std::string>(); break;
                        case scene_value_type_e::VEC3:
                            prop.vec = vec3_t(v[0].get<f32>(), v[1].get<f32>(), v[2].get<f32>());
                            break;
                        case scene_value_type_e::ASSET_REF:
                            prop.asset_type = v.value("t", u64_t{0});
                            prop.str = v.value("p", std::string{});
                            prop.guid = v.value("g", std::string{});
                            break;
                        case scene_value_type_e::ASSET_REF_LIST:
                        {
                            prop.asset_type = v.value("t", u64_t{0});
                            const nlohmann::json entries = v.value("e", nlohmann::json::array());
                            for (const nlohmann::json& entry : entries)
                            {
                                prop.strs.push_back(entry.value("p", std::string{}));
                                prop.guids.push_back(entry.value("g", std::string{}));
                            }
                            break;
                        }
                        }
                    }
                    else
                    {
                        TAU_LOG_WARN("SCENE", "Property '{}' is not a tagged value -- skipped", prop_key);
                        continue;
                    }

                    out_comp.properties.push_back(std::move(prop));
                }
            }
        }

        return scene;
    }

    void write_scene_binary(const scene_t& scene, const std::string& output_path)
    {
        std::ofstream out(output_path, std::ios::binary);
        if (!out.is_open())
        {
            TAU_LOG_ERROR("SCENE", "Failed to open output for scene: {}", output_path);
            return;
        }

        scene_header_t header;
        header.entity_count = static_cast<u32_t>(scene.entities.size());
        write_pod(out, header);

        for (const scene_entity_t& entity : scene.entities)
        {
            write_pod(out, static_cast<u32_t>(entity.components.size()));
            for (const scene_component_t& comp : entity.components)
            {
                write_pod(out, comp.type_hash);
                write_pod(out, static_cast<u32_t>(comp.properties.size()));
                for (const scene_property_t& prop : comp.properties)
                {
                    write_pod(out, prop.prop_hash);
                    write_pod(out, static_cast<u8_t>(prop.type));
                    switch (prop.type)
                    {
                    case scene_value_type_e::I32: write_pod(out, prop.i); break;
                    case scene_value_type_e::U32: write_pod(out, prop.u); break;
                    case scene_value_type_e::F32: write_pod(out, prop.f); break;
                    case scene_value_type_e::BOOL: write_pod(out, static_cast<u8_t>(prop.b ? 1 : 0)); break;
                    case scene_value_type_e::VEC3:
                        write_pod(out, prop.vec.x);
                        write_pod(out, prop.vec.y);
                        write_pod(out, prop.vec.z);
                        break;
                    case scene_value_type_e::STRING: write_str(out, prop.str); break;
                    case scene_value_type_e::ASSET_REF:
                        write_pod(out, prop.asset_type);
                        write_str(out, prop.str);
                        write_str(out, prop.guid);
                        break;
                    case scene_value_type_e::ASSET_REF_LIST:
                        write_pod(out, prop.asset_type);
                        write_pod(out, static_cast<u32_t>(prop.strs.size()));
                        for (std::size_t i = 0; i < prop.strs.size(); i++)
                        {
                            write_str(out, prop.strs[i]);
                            write_str(out, i < prop.guids.size() ? prop.guids[i] : std::string{});
                        }
                        break;
                    }
                }
            }
        }
    }

    namespace
    {
        void apply_scene_entity(tau::world_t& world, tau::ecs::entity_t entity, const scene_entity_t& scene_entity)
        {
            for (const scene_component_t& comp : scene_entity.components)
            {
                const u32_t type_hash = comp.type_hash;
                tau::reflection::type_t type = tau::reflection::resolve(*world.reflection_ctx, type_hash);

                if (!type)
                {
                    TAU_LOG_WARN("SCENE", "Unknown component '{}', dropped from entity",
                                 describe_key(comp.type_name, comp.type_hash));
                    continue;
                }

                if (tau::reflection::func_t add_func = type.func("add"_h); add_func)
                {
                    add_func.invoke({}, tau::reflection::forward_as_meta(world.registry), entity);
                }

                tau::reflection::func_t get_func = type.func("get"_h);
                if (!get_func) { continue; }

                tau::reflection::any_t instance =
                    get_func.invoke({}, tau::reflection::forward_as_meta(world.registry), entity);
                if (!instance) { continue; }

                for (const scene_property_t& prop : comp.properties)
                {
                    tau::reflection::data_t data = type.data(prop.prop_hash);

                    if (!data)
                    {
                        TAU_LOG_WARN("SCENE", "Unknown field '{}' on component '{}', value dropped",
                                     describe_key(prop.prop_name, prop.prop_hash),
                                     describe_key(comp.type_name, comp.type_hash));
                        continue;
                    }

                    switch (prop.type)
                    {
                    case scene_value_type_e::I32: data.set(instance, prop.i); break;
                    case scene_value_type_e::U32: data.set(instance, prop.u); break;
                    case scene_value_type_e::F32: data.set(instance, prop.f); break;
                    case scene_value_type_e::BOOL: data.set(instance, prop.b); break;
                    case scene_value_type_e::STRING: data.set(instance, prop.str); break;
                    case scene_value_type_e::VEC3: data.set(instance, prop.vec); break;
                    case scene_value_type_e::ASSET_REF:
                    {
                        const std::string ref = tau::asset_meta::resolve_ref(prop.guid, prop.str);
                        if (!ref.empty())
                        {
                            asset_handle_t handle =
                                tau::asset_serde::load(prop.asset_type, tau::engine::get_asset_registry(), ref);
                            data.set(instance, handle);
                        }
                        break;
                    }
                    case scene_value_type_e::ASSET_REF_LIST:
                    {
                        std::vector<asset_handle_t> list;
                        list.reserve(prop.strs.size());
                        for (std::size_t i = 0; i < prop.strs.size(); i++)
                        {
                            const std::string ref = tau::asset_meta::resolve_ref(
                                i < prop.guids.size() ? prop.guids[i] : std::string{}, prop.strs[i]);
                            list.push_back(ref.empty() ? asset_handle_t{}
                                                       : tau::asset_serde::load(
                                                             prop.asset_type, tau::engine::get_asset_registry(), ref));
                        }
                        data.set(instance, list);
                        break;
                    }
                    }
                }

                if (tau::reflection::func_t on_changed = type.func("on_changed"_h); on_changed)
                {
                    on_changed.invoke({}, tau::reflection::forward_as_meta(world.registry), entity);
                }
            }
        }
    } // namespace

    void instantiate_scene(tau::world_t& world, const scene_t& scene)
    {
        for (const scene_entity_t& scene_entity : scene.entities)
        {
            tau::ecs::entity_t entity = world.registry.create();
            apply_scene_entity(world, entity, scene_entity);
        }
    }

    void deserialize_scene(tau::world_t& world, const nlohmann::json& scene_data)
    { instantiate_scene(world, scene_from_json(scene_data)); }

    void clear_scene(tau::world_t& world)
    {
        auto view = world.registry.view<tau::ecs::entity_t>();

        std::vector<tau::ecs::entity_t> to_destroy;
        to_destroy.insert(to_destroy.end(), view.begin(), view.end());

        world.registry.destroy(to_destroy.begin(), to_destroy.end());
    }

    std::unordered_set<u32_t> reflected_component_pool_ids(tau::world_t& world)
    {
        std::unordered_set<u32_t> ids;
        for (auto [meta_id, type] : tau::reflection::resolve(*world.reflection_ctx))
        {
            ids.insert(static_cast<u32_t>(type.info().hash()));
        }
        return ids;
    }

    void create_registered_pools(tau::world_t& world)
    {
        for (auto [meta_id, type] : tau::reflection::resolve(*world.reflection_ctx))
        {
            const tau::reflection::func_t storage = type.func("storage"_h);
            if (storage) { storage.invoke({}, tau::reflection::forward_as_meta(world.registry)); }
        }
    }

    reload_snapshot_t evict_game_components(tau::world_t& world, const std::unordered_set<u32_t>& engine_pool_ids)
    {
        reload_snapshot_t snapshot;

        std::unordered_map<u32_t, tau::reflection::type_t> game_types;
        for (const auto& [pool_id, type] : component_types_by_pool(world))
        {
            if (!engine_pool_ids.count(pool_id)) { game_types.emplace(pool_id, type); }
        }

        std::unordered_map<tau::ecs::entity_t, std::size_t> slots;
        std::vector<std::pair<tau::ecs::entity_t, nlohmann::json>> collected;

        for (auto [pool_id, pool] : world.registry.storage())
        {
            const auto type_it = game_types.find(static_cast<u32_t>(pool_id));
            if (type_it == game_types.end()) { continue; }

            const tau::reflection::type_t type = type_it->second;
            const tau::reflection::func_t get_func = type.func("get"_h);
            const std::string type_key = meta_key(type.name(), static_cast<u32_t>(type.id()));

            for (tau::ecs::entity_t entity : pool)
            {
                tau::reflection::any_t instance =
                    get_func.invoke({}, tau::reflection::forward_as_meta(world.registry), entity);
                if (!instance) { continue; }

                const auto [slot_it, inserted] = slots.try_emplace(entity, collected.size());
                if (inserted) { collected.emplace_back(entity, nlohmann::json::object()); }

                collected[slot_it->second].second[type_key] = encode_component_fields(world, type, instance);
            }
        }

        for (auto& [entity, comps] : collected)
        {
            snapshot.entities.push_back({
                {"e",          static_cast<u32_t>(entt::to_integral(entity))},
                {"components", std::move(comps)                             }
            });
        }

        for (const auto& [pool_id, type] : game_types) { world.registry.reset(pool_id); }

        return snapshot;
    }

    void restore_game_components(tau::world_t& world, const reload_snapshot_t& snapshot)
    {
        for (const nlohmann::json& entry : snapshot.entities)
        {
            tau::ecs::entity_t entity = static_cast<tau::ecs::entity_t>(entry.at("e").get<u32_t>());
            if (!world.registry.valid(entity)) { continue; }

            nlohmann::json wrapper;
            wrapper["entities"] = nlohmann::json::array();
            wrapper["entities"].push_back({
                {"components", entry.at("components")}
            });

            scene_t scene = scene_from_json(wrapper);
            if (!scene.entities.empty()) { apply_scene_entity(world, entity, scene.entities.front()); }
        }
    }
} // namespace tau::serialization