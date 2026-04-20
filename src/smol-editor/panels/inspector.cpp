#include "inspector.h"

#include "imgui.h"
#include "smol/ecs_fwd.h"
#include "smol/hash.h"
#include "smol/reflection.h"

namespace smol::editor::panels
{
    namespace
    {
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

        bool draw_meta_any(smol::world_t& world, smol::reflection::any_t& instance)
        {
            bool was_modified = false;
            smol::reflection::type_t type = instance.type();

            for (auto [id, data] : type.data())
            {
                smol::reflection::editor_prop_t* prop = data.custom();
                const char* label = prop ? prop->name : "Unknown";

                smol::reflection::type_t field_type = data.type();
                smol::reflection::any_t field_value = data.get(instance);

                if (field_type == smol::reflection::resolve<i32_t>(*world.reflection_ctx))
                {
                    i32_t val = field_value.cast<i32_t>();
                    if (ImGui::DragInt(label, &val))
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
                else if (field_type == smol::reflection::resolve<smol::vec3_t>(*world.reflection_ctx))
                {
                    smol::vec3_t vec = field_value.cast<smol::vec3_t>();
                    if (ImGui::DragFloat3(label, &vec.x, 0.1f))
                    {
                        data.set(instance, vec);
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
                    if (smol::reflection::func_t get_func = type.func("get_component"_h); get_func)
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
                                bool changed = draw_meta_any(world, instance);

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
                        smol::reflection::func_t get_func = type.func("get_component"_h);

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