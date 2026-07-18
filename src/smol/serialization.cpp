#include "serialization.h"

#include "entt/meta/meta.hpp"
#include "entt/meta/resolve.hpp"
#include "smol/asset.h"
#include "smol/asset_meta.h"
#include "smol/asset_serde.h"
#include "smol/assets/scene_format.h"
#include "smol/ecs_fwd.h"
#include "smol/engine.h"
#include "smol/hash.h"
#include "smol/log.h"
#include "smol/math.h"
#include "smol/reflection.h"

#include "json/json.hpp"
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace smol::serialization
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

        template <typename T>
        void write_pod(std::ofstream& out, const T& v)
        { out.write(reinterpret_cast<const char*>(&v), sizeof(T)); }

        void write_str(std::ofstream& out, const std::string& s)
        {
            u32_t len = static_cast<u32_t>(s.size());
            write_pod(out, len);
            out.write(s.data(), s.size());
        }
    } // namespace

    nlohmann::json serialize_scene(smol::world_t& world)
    {
        nlohmann::json scene_data;
        scene_data["entities"] = nlohmann::json::array();

        for (smol::ecs::entity_t entity : world.registry.view<smol::ecs::entity_t>())
        {
            nlohmann::json entity_json;
            entity_json["components"] = nlohmann::json::object();

            for (auto [internal_type_id, type] : smol::reflection::resolve(*world.reflection_ctx))
            {
                u32_t type_id = static_cast<u32_t>(type.id());

                smol::reflection::func_t get_func = type.func("get"_h);
                if (!get_func) { continue; }

                smol::reflection::any_t instance =
                    get_func.invoke({}, smol::reflection::forward_as_meta(world.registry), entity);
                if (!instance) { continue; }

                std::string type_hash_str = std::to_string(type_id);
                nlohmann::json& comp_json = entity_json["components"][type_hash_str];
                comp_json = nlohmann::json::object();

                for (auto [data_id, data] : type.data())
                {
                    smol::reflection::type_t field_type = data.type();
                    smol::reflection::any_t field_value = data.get(instance);
                    std::string prop_hash_str = std::to_string(data_id);

                    smol::reflection::editor_prop_t* prop =
                        static_cast<smol::reflection::editor_prop_t*>(data.custom());
                    if (prop && prop->asset_type_hash != 0)
                    {
                        asset_handle_t handle = field_value.cast<asset_handle_t>();
                        std::string path = smol::engine::get_asset_registry().get_path(handle);
                        std::string_view guid = smol::asset_meta::get_guid(path);
                        comp_json[prop_hash_str] =
                            tagged(scene_value_type_e::ASSET_REF, {
                                                                      {"t", prop->asset_type_hash                },
                                                                      {"g", guid.empty() ? "" : std::string(guid)},
                                                                      {"p", path                                 }
                        });
                        continue;
                    }

                    if (field_type == smol::reflection::resolve<i32>(*world.reflection_ctx))
                    {
                        comp_json[prop_hash_str] = tagged(scene_value_type_e::I32, field_value.cast<i32>());
                    }
                    else if (field_type == smol::reflection::resolve<i32_t>(*world.reflection_ctx))
                    {
                        comp_json[prop_hash_str] = tagged(scene_value_type_e::I32, field_value.cast<i32_t>());
                    }
                    else if (field_type == smol::reflection::resolve<u32>(*world.reflection_ctx))
                    {
                        comp_json[prop_hash_str] = tagged(scene_value_type_e::U32, field_value.cast<u32>());
                    }
                    else if (field_type == smol::reflection::resolve<u32_t>(*world.reflection_ctx))
                    {
                        comp_json[prop_hash_str] = tagged(scene_value_type_e::U32, field_value.cast<u32_t>());
                    }
                    else if (field_type == smol::reflection::resolve<f32>(*world.reflection_ctx))
                    {
                        comp_json[prop_hash_str] = tagged(scene_value_type_e::F32, field_value.cast<f32>());
                    }
                    else if (field_type == smol::reflection::resolve<bool>(*world.reflection_ctx))
                    {
                        comp_json[prop_hash_str] = tagged(scene_value_type_e::BOOL, field_value.cast<bool>());
                    }
                    else if (field_type == smol::reflection::resolve<std::string>(*world.reflection_ctx))
                    {
                        comp_json[prop_hash_str] = tagged(scene_value_type_e::STRING, field_value.cast<std::string>());
                    }
                    else if (field_type == smol::reflection::resolve<vec3_t>(*world.reflection_ctx))
                    {
                        vec3_t vec = field_value.cast<smol::vec3_t>();
                        comp_json[prop_hash_str] = tagged(scene_value_type_e::VEC3, {vec.x, vec.y, vec.z});
                    }
                }
            }

            scene_data["entities"].push_back(entity_json);
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

            for (const auto& [type_hash_str, props_json] : entity_json["components"].items())
            {
                scene_component_t& out_comp = out_entity.components.emplace_back();
                out_comp.type_hash = static_cast<u32_t>(std::stoul(type_hash_str));

                for (const auto& [prop_hash_str, val] : props_json.items())
                {
                    scene_property_t prop;
                    prop.prop_hash = static_cast<u32_t>(std::stoul(prop_hash_str));

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
                            break;
                        }
                    }
                    else if (val.is_boolean())
                    {
                        prop.type = scene_value_type_e::BOOL;
                        prop.b = val.get<bool>();
                    }
                    else if (val.is_object() && val.contains("t"))
                    {
                        prop.type = scene_value_type_e::ASSET_REF;
                        prop.asset_type = val.value("t", u64_t{0});
                        prop.str = val.value("p", std::string{});
                    }
                    else if (val.is_array())
                    {
                        prop.type = scene_value_type_e::VEC3;
                        prop.vec = vec3_t(val[0].get<f32>(), val[1].get<f32>(), val[2].get<f32>());
                    }
                    else if (val.is_string())
                    {
                        prop.type = scene_value_type_e::STRING;
                        prop.str = val.get<std::string>();
                    }
                    else if (val.is_number_float())
                    {
                        prop.type = scene_value_type_e::F32;
                        prop.f = val.get<f32>();
                    }
                    else
                    {
                        prop.type = scene_value_type_e::I32;
                        prop.i = val.get<i32_t>();
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
            SMOL_LOG_ERROR("SCENE", "Failed to open output for scene: {}", output_path);
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
                        break;
                    }
                }
            }
        }
    }

    void instantiate_scene(smol::world_t& world, const scene_t& scene)
    {
        for (const scene_entity_t& scene_entity : scene.entities)
        {
            smol::ecs::entity_t entity = world.registry.create();

            for (const scene_component_t& comp : scene_entity.components)
            {
                smol::reflection::type_t type = smol::reflection::resolve(*world.reflection_ctx, comp.type_hash);
                if (!type) { continue; }

                if (smol::reflection::func_t add_func = type.func("add"_h); add_func)
                {
                    add_func.invoke({}, smol::reflection::forward_as_meta(world.registry), entity);
                }

                smol::reflection::func_t get_func = type.func("get"_h);
                if (!get_func) { continue; }

                smol::reflection::any_t instance =
                    get_func.invoke({}, smol::reflection::forward_as_meta(world.registry), entity);
                if (!instance) { continue; }

                for (const scene_property_t& prop : comp.properties)
                {
                    smol::reflection::data_t data = type.data(prop.prop_hash);
                    if (!data) { continue; }

                    switch (prop.type)
                    {
                    case scene_value_type_e::I32: data.set(instance, prop.i); break;
                    case scene_value_type_e::U32: data.set(instance, prop.u); break;
                    case scene_value_type_e::F32: data.set(instance, prop.f); break;
                    case scene_value_type_e::BOOL: data.set(instance, prop.b); break;
                    case scene_value_type_e::STRING: data.set(instance, prop.str); break;
                    case scene_value_type_e::VEC3: data.set(instance, prop.vec); break;
                    case scene_value_type_e::ASSET_REF:
                        if (!prop.str.empty())
                        {
                            asset_handle_t handle =
                                smol::asset_serde::load(prop.asset_type, smol::engine::get_asset_registry(), prop.str);
                            data.set(instance, handle);
                        }
                        break;
                    }
                }

                if (smol::reflection::func_t on_changed = type.func("on_changed"_h); on_changed)
                {
                    on_changed.invoke({}, smol::reflection::forward_as_meta(world.registry), entity);
                }
            }
        }
    }

    void deserialize_scene(smol::world_t& world, const nlohmann::json& scene_data)
    { instantiate_scene(world, scene_from_json(scene_data)); }

    void clear_scene(smol::world_t& world)
    {
        auto view = world.registry.view<smol::ecs::entity_t>();

        std::vector<smol::ecs::entity_t> to_destroy;
        to_destroy.insert(to_destroy.end(), view.begin(), view.end());

        world.registry.destroy(to_destroy.begin(), to_destroy.end());
    }
} // namespace smol::serialization
