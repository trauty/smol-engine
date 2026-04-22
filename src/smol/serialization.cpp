#include "serialization.h"

#include "entt/meta/meta.hpp"
#include "entt/meta/resolve.hpp"
#include "smol-editor/systems/camera.h"
#include "smol/asset.h"
#include "smol/assets/material.h"
#include "smol/assets/mesh.h"
#include "smol/ecs_fwd.h"
#include "smol/hash.h"
#include "smol/log.h"
#include "smol/math.h"
#include "smol/reflection.h"

#include "json/json.hpp"
#include <string>
#include <vector>

namespace smol::serialization
{
    nlohmann::json serialize_scene(smol::world_t& world)
    {
        nlohmann::json scene_data;
        scene_data["entities"] = nlohmann::json::array();

        for (smol::ecs::entity_t entity : world.registry.view<smol::ecs::entity_t>())
        {
            if (world.registry.all_of<smol::editor::editor_camera_tag>(entity)) { continue; }

            nlohmann::json entity_json;
            entity_json["components"] = nlohmann::json::object();

            for (auto [internal_type_id, type] : smol::reflection::resolve(*world.reflection_ctx))
            {
                u32_t type_id = static_cast<u32_t>(type.id());

                if (smol::reflection::func_t get_func = type.func("get"_h); get_func)
                {
                    smol::reflection::any_t instance =
                        get_func.invoke({}, smol::reflection::forward_as_meta(world.registry), entity);

                    if (instance)
                    {
                        std::string type_hash_str = std::to_string(type_id);
                        entity_json["components"][type_hash_str] = nlohmann::json::object();

                        for (auto [data_id, data] : type.data())
                        {
                            smol::reflection::type_t field_type = data.type();
                            smol::reflection::any_t field_value = data.get(instance);
                            std::string prop_hash_str = std::to_string(data_id);

                            if (field_type == smol::reflection::resolve<i32>(*world.reflection_ctx))
                            {
                                entity_json["components"][type_hash_str][prop_hash_str] = field_value.cast<i32>();
                            }
                            else if (field_type == smol::reflection::resolve<i32_t>(*world.reflection_ctx))
                            {
                                entity_json["components"][type_hash_str][prop_hash_str] = field_value.cast<i32_t>();
                            }
                            else if (field_type == smol::reflection::resolve<u32>(*world.reflection_ctx))
                            {
                                entity_json["components"][type_hash_str][prop_hash_str] = field_value.cast<u32>();
                            }
                            else if (field_type == smol::reflection::resolve<u32_t>(*world.reflection_ctx))
                            {
                                entity_json["components"][type_hash_str][prop_hash_str] = field_value.cast<u32_t>();
                            }
                            else if (field_type == smol::reflection::resolve<f32>(*world.reflection_ctx))
                            {
                                entity_json["components"][type_hash_str][prop_hash_str] = field_value.cast<f32>();
                            }
                            else if (field_type == smol::reflection::resolve<bool>(*world.reflection_ctx))
                            {
                                entity_json["components"][type_hash_str][prop_hash_str] = field_value.cast<bool>();
                            }
                            else if (field_type == smol::reflection::resolve<std::string>(*world.reflection_ctx))
                            {
                                entity_json["components"][type_hash_str][prop_hash_str] =
                                    field_value.cast<std::string>();
                            }
                            else if (field_type == smol::reflection::resolve<vec3_t>(*world.reflection_ctx))
                            {
                                vec3_t vec = field_value.cast<smol::vec3_t>();
                                entity_json["components"][type_hash_str][prop_hash_str] = {vec.x, vec.y, vec.z};
                            }
                            else if (field_type ==
                                     smol::reflection::resolve<smol::asset_t<smol::mesh_t>>(*world.reflection_ctx))
                            {
                                smol::asset_t<smol::mesh_t> handle = field_value.cast<smol::asset_t<smol::mesh_t>>();
                                entity_json["components"][type_hash_str][prop_hash_str] = handle.slot->path;
                            }
                            else if (field_type ==
                                     smol::reflection::resolve<smol::asset_t<smol::material_t>>(*world.reflection_ctx))
                            {
                                smol::asset_t<smol::material_t> handle =
                                    field_value.cast<smol::asset_t<smol::material_t>>();
                                entity_json["components"][type_hash_str][prop_hash_str] = handle.slot->path;
                            }
                        }
                    }
                }
            }

            scene_data["entities"].push_back(entity_json);
        }

        return scene_data;
    }
    void deserialize_scene(smol::world_t& world, const nlohmann::json& scene_data)
    {
        for (const nlohmann::json& entity_json : scene_data["entities"])
        {
            smol::ecs::entity_t entity = world.registry.create();

            for (const auto& [type_hash_str, props_json] : entity_json["components"].items())
            {
                u32_t type_id = std::stoull(type_hash_str);
                smol::reflection::type_t type = smol::reflection::resolve(*world.reflection_ctx, type_id);

                if (type)
                {
                    if (smol::reflection::func_t add_func = type.func("add"_h); add_func)
                    {
                        add_func.invoke({}, smol::reflection::forward_as_meta(world.registry), entity);
                    }

                    if (smol::reflection::func_t get_func = type.func("get"_h); get_func)
                    {
                        smol::reflection::any_t instance =
                            get_func.invoke({}, smol::reflection::forward_as_meta(world.registry), entity);

                        if (instance)
                        {
                            for (const auto& [props_hash_str, prop_val] : props_json.items())
                            {
                                u32_t prop_id = std::stoull(props_hash_str);
                                smol::reflection::data_t data = type.data(prop_id);

                                if (data)
                                {
                                    smol::reflection::type_t field_type = data.type();

                                    if (field_type == smol::reflection::resolve<i32>(*world.reflection_ctx))
                                    {
                                        data.set(instance, prop_val.get<i32>());
                                    }
                                    else if (field_type == smol::reflection::resolve<i32_t>(*world.reflection_ctx))
                                    {
                                        data.set(instance, prop_val.get<i32_t>());
                                    }
                                    if (field_type == smol::reflection::resolve<u32>(*world.reflection_ctx))
                                    {
                                        data.set(instance, prop_val.get<u32>());
                                    }
                                    else if (field_type == smol::reflection::resolve<u32_t>(*world.reflection_ctx))
                                    {
                                        data.set(instance, prop_val.get<u32_t>());
                                    }
                                    else if (field_type == smol::reflection::resolve<f32>(*world.reflection_ctx))
                                    {
                                        data.set(instance, prop_val.get<f32>());
                                    }
                                    else if (field_type == smol::reflection::resolve<bool>(*world.reflection_ctx))
                                    {
                                        data.set(instance, prop_val.get<bool>());
                                    }
                                    else if (field_type ==
                                             smol::reflection::resolve<std::string>(*world.reflection_ctx))
                                    {
                                        data.set(instance, prop_val.get<std::string>());
                                    }
                                    else if (field_type == smol::reflection::resolve<vec3_t>(*world.reflection_ctx))
                                    {
                                        data.set(instance, vec3_t(prop_val[0], prop_val[1], prop_val[2]));
                                    }
                                    else if (field_type == smol::reflection::resolve<smol::asset_t<smol::mesh_t>>(
                                                               *world.reflection_ctx))
                                    {
                                        std::string asset_path = prop_val.get<std::string>();
                                        if (!asset_path.empty())
                                        {
                                            smol::asset_t<smol::mesh_t> loaded_mesh =
                                                smol::load_asset_sync<smol::mesh_t>(asset_path);
                                            data.set(instance, loaded_mesh);
                                        }
                                    }
                                    else if (field_type == smol::reflection::resolve<smol::asset_t<smol::material_t>>(
                                                               *world.reflection_ctx))
                                    {
                                        std::string asset_path = prop_val.get<std::string>();
                                        if (!asset_path.empty())
                                        {
                                            smol::asset_t<smol::material_t> loaded_mat =
                                                smol::load_asset_sync<>(asset_path);
                                            data.set(instance, loaded_mat);
                                        }
                                    }
                                }
                            }

                            if (smol::reflection::func_t on_changed = type.func("on_changed"_h); on_changed)
                            {
                                on_changed.invoke({}, smol::reflection::forward_as_meta(world.registry), entity);
                            }
                        }
                    }
                }
            }
        };
    }

    void clear_scene(smol::world_t& world)
    {
        auto view = world.registry.view<smol::ecs::entity_t>(entt::exclude<editor::editor_camera_tag>);

        std::vector<smol::ecs::entity_t> to_destroy;
        to_destroy.insert(to_destroy.end(), view.begin(), view.end());

        world.registry.destroy(to_destroy.begin(), to_destroy.end());
    }
} // namespace smol::serialization