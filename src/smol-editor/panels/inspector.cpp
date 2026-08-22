#include "inspector.h"

#include "imgui.h"
#include "smol/asset.h"
#include "smol/asset_serde.h"
#include "smol/ecs_fwd.h"
#include "smol/engine.h"
#include "smol/hash.h"
#include "smol/reflection.h"

#include <cmath>

namespace smol::editor::panels
{
    namespace
    {
        char asset_path_input[1024] = {0};

        // radians -> quat -> radians not stable
        struct degree_edit_t
        {
            ImGuiID widget = 0;
            smol::vec3_t degrees{};
        };

        degree_edit_t active_degree_edit;

        i32 string_resize_cb(ImGuiInputTextCallbackData* data)
        {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
            {
                std::string* str = static_cast<std::string*>(data->UserData);
                str->resize(data->BufTextLen);
                data->Buf = str->data();
            }

            return 0;
        }

        bool enum_to_i32(const smol::reflection::any_t& value, i32& out)
        {
            const smol::reflection::any_t converted = value.allow_cast<i32>();
            if (!converted) { return false; }

            out = converted.cast<i32>();
            return true;
        }

        bool draw_meta_any(smol::world_t& world, smol::reflection::any_t& instance, smol::ecs::entity_t entity)
        {
            bool was_modified = false;
            smol::reflection::type_t type = instance.type();

            for (auto [id, data] : type.data())
            {
                smol::reflection::editor_prop_t* prop = data.custom();
                const char* label = prop ? prop->name : "Unknown";

                smol::reflection::type_t field_type = data.type();
                smol::reflection::any_t field_value = data.get(instance);

                if (prop && prop->asset_type_hash != 0)
                {
                    asset_handle_t handle = field_value.cast<asset_handle_t>();
                    std::string cur_path = smol::engine::get_asset_registry().get_path(handle);

                    ImGui::Text("%s", label);
                    ImGui::SameLine();

                    if (cur_path.empty()) { ImGui::TextDisabled("None"); }
                    else
                    {
                        ImGui::TextUnformatted(cur_path.c_str());
                    }

                    ImGui::SameLine();
                    std::string popup_id = std::string("##asset_") + std::to_string(id);
                    if (ImGui::SmallButton(("..." + popup_id).c_str()))
                    {
                        std::strncpy(asset_path_input, cur_path.c_str(), sizeof(asset_path_input) - 1);
                        asset_path_input[sizeof(asset_path_input) - 1] = '\0';
                        ImGui::OpenPopup(popup_id.c_str());
                    }

                    if (ImGui::BeginPopup(popup_id.c_str()))
                    {
                        ImGui::Text("Asset Path:");
                        if (ImGui::InputText("##path_input", asset_path_input, sizeof(asset_path_input),
                                             ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            std::string new_path(asset_path_input);
                            if (!new_path.empty())
                            {
                                asset_handle_t new_handle = smol::asset_serde::load(
                                    prop->asset_type_hash, smol::engine::get_asset_registry(), new_path);
                                data.set(instance, new_handle);
                            }
                            else
                            {
                                data.set(instance, asset_handle_t{});
                            }
                            was_modified = true;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    if (was_modified) { continue; }
                }

                if (field_type == smol::reflection::resolve<i32>(*world.reflection_ctx))
                {
                    i32 val = field_value.cast<i32>();
                    if (ImGui::DragInt(label, &val))
                    {
                        data.set(instance, val);
                        was_modified = true;
                    }
                }
                else if (field_type == smol::reflection::resolve<u32>(*world.reflection_ctx))
                {
                    u32 val = field_value.cast<u32>();
                    if (ImGui::DragScalar(label, ImGuiDataType_U32, &val))
                    {
                        data.set(instance, val);
                        was_modified = true;
                    }
                }
                else if (field_type == smol::reflection::resolve<f32>(*world.reflection_ctx))
                {
                    f32 val = field_value.cast<f32>();
                    if (ImGui::DragFloat(label, &val, 0.1f))
                    {
                        data.set(instance, val);
                        was_modified = true;
                    }
                }
                else if (field_type == smol::reflection::resolve<bool>(*world.reflection_ctx))
                {
                    bool val = field_value.cast<bool>();
                    if (ImGui::Checkbox(label, &val))
                    {
                        data.set(instance, val);
                        was_modified = true;
                    }
                }
                else if (field_type == smol::reflection::resolve<std::string>(*world.reflection_ctx))
                {
                    std::basic_string<char> str = field_value.cast<std::string>();
                    if (str.capacity() < 32) { str.reserve(32); }

                    if (ImGui::InputText(label, str.data(), str.capacity() + 1, ImGuiInputTextFlags_CallbackResize,
                                         string_resize_cb, &str))
                    {
                        data.set(instance, std::string(str.data()));
                        was_modified = true;
                    }
                }
                else if (field_type == smol::reflection::resolve<smol::vec3_t>(*world.reflection_ctx))
                {
                    smol::vec3_t vec = field_value.cast<smol::vec3_t>();

                    if (prop && prop->unit == smol::reflection::unit_e::RADIANS)
                    {
                        constexpr f32 RAD2DEG = 57.2957795f;
                        constexpr f32 DEG2RAD = 0.0174532925f;

                        auto wrap_deg = [](f32 deg) { return std::fmod(deg, 360.0f); };

                        const ImGuiID widget = ImGui::GetID(label);

                        smol::vec3_t deg = {wrap_deg(vec.x * RAD2DEG), wrap_deg(vec.y * RAD2DEG),
                                            wrap_deg(vec.z * RAD2DEG)};
                        if (active_degree_edit.widget == widget) { deg = active_degree_edit.degrees; }

                        if (ImGui::DragFloat3(label, &deg.x, 0.5f))
                        {
                            deg = {wrap_deg(deg.x), wrap_deg(deg.y), wrap_deg(deg.z)};

                            smol::vec3_t rad = {deg.x * DEG2RAD, deg.y * DEG2RAD, deg.z * DEG2RAD};
                            data.set(instance, rad);
                            was_modified = true;
                        }

                        if (ImGui::IsItemActive())
                        {
                            active_degree_edit.widget = widget;
                            active_degree_edit.degrees = deg;
                        }
                        else if (active_degree_edit.widget == widget) { active_degree_edit = degree_edit_t{}; }
                    }
                    else if (ImGui::DragFloat3(label, &vec.x, 0.1f))
                    {
                        data.set(instance, vec);
                        was_modified = true;
                    }
                }
                else if (field_type.is_enum())
                {
                    i32 cur_val = 0;
                    if (!enum_to_i32(field_value, cur_val)) { continue; }

                    const char* preview_name = "Unknown";
                    for (auto [enum_data_id, enum_data] : field_type.data())
                    {
                        i32 candidate = 0;
                        if (!enum_to_i32(enum_data.get({}), candidate)) { continue; }

                        if (candidate == cur_val)
                        {
                            const auto* enum_prop = static_cast<smol::reflection::editor_prop_t*>(enum_data.custom());
                            preview_name = enum_prop ? enum_prop->name : "Selected";
                        }
                    }

                    if (ImGui::BeginCombo(label, preview_name))
                    {
                        for (auto [enum_data_id, enum_data] : field_type.data())
                        {
                            i32 enum_val = 0;
                            if (!enum_to_i32(enum_data.get({}), enum_val)) { continue; }

                            const smol::reflection::editor_prop_t* enum_prop =
                                static_cast<smol::reflection::editor_prop_t*>(enum_data.custom());
                            const char* enum_name = enum_prop ? enum_prop->name : "Option";

                            bool is_selected = (cur_val == enum_val);
                            if (ImGui::Selectable(enum_name, is_selected))
                            {
                                data.set(instance, enum_val);
                                was_modified = true;
                            }
                            if (is_selected) { ImGui::SetItemDefaultFocus(); }
                        }

                        ImGui::EndCombo();
                    }
                }
            }

            return was_modified;
        }
    } // namespace

    void draw_inspector(world_t& world, editor_context_t& ctx)
    {
        ecs::entity_t selected_entity = ctx.selected_entity;

        if (ImGui::Begin("Inspector"))
        {
            if (selected_entity != smol::ecs::NULL_ENTITY && world.registry.valid(selected_entity))
            {
                for (auto [id, type] : smol::reflection::resolve(*world.reflection_ctx))
                {
                    if (smol::reflection::func_t get_func = type.func("get"_h); get_func)
                    {
                        smol::reflection::any_t instance =
                            get_func.invoke({}, smol::reflection::forward_as_meta(world.registry), selected_entity);

                        if (instance)
                        {
                            smol::reflection::editor_prop_t* type_prop = type.custom();
                            const char* header_name = type_prop ? type_prop->name : "UnknownComponent";

                            ImGui::PushID(static_cast<i32>(id));

                            bool header_open = ImGui::CollapsingHeader(
                                header_name, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

                            ImGui::SameLine(ImGui::GetWindowWidth() - 30.0f);
                            if (ImGui::Button("X"))
                            {
                                if (smol::reflection::func_t remove_func = type.func("remove"_h); remove_func)
                                {
                                    remove_func.invoke({}, smol::reflection::forward_as_meta(world.registry),
                                                       selected_entity);
                                }
                            }

                            if (header_open)
                            {
                                bool changed = draw_meta_any(world, instance, selected_entity);

                                if (changed)
                                {
                                    if (smol::reflection::func_t on_changed = type.func("on_changed"_h); on_changed)
                                    {
                                        on_changed.invoke({}, smol::reflection::forward_as_meta(world.registry),
                                                          selected_entity);
                                    }
                                }
                            }

                            ImGui::PopID();
                        }
                    }
                }

                ImGui::Separator();

                if (ImGui::Button("Add Component")) { ImGui::OpenPopup("AddComponentPopup"); }

                if (ImGui::BeginPopup("AddComponentPopup"))
                {
                    for (auto [id, type] : smol::reflection::resolve(*world.reflection_ctx))
                    {
                        smol::reflection::func_t add_func = type.func("add"_h);
                        smol::reflection::func_t get_func = type.func("get"_h);

                        if (add_func && get_func)
                        {
                            smol::reflection::any_t instance =
                                get_func.invoke({}, smol::reflection::forward_as_meta(world.registry), selected_entity);

                            if (!instance)
                            {
                                smol::reflection::editor_prop_t* type_prop = type.custom();
                                const char* menu_name = type_prop ? type_prop->name : "UnknownComponent";

                                if (ImGui::MenuItem(menu_name))
                                {
                                    add_func.invoke({}, smol::reflection::forward_as_meta(world.registry),
                                                    selected_entity);
                                    ImGui::CloseCurrentPopup();
                                }
                            }
                        }
                    }

                    ImGui::EndPopup();
                }
            }
        }

        ImGui::End();
    }
} // namespace smol::editor::panels