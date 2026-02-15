#include "shader.h"

#include "smol/asset.h"
#include "smol/assets/mesh.h"
#include "smol/color.h"
#include "smol/log.h"
#include "smol/main_thread.h"
#include "smol/math.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/shader_compiler.h"
#include "smol/util.h"

#include <cstddef>
#include <cstring>
#include <optional>
#include <slang-com-ptr.h>
#include <slang.h>
#include <stack>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace smol
{
    namespace
    {
        struct slang_compilation_res_t
        {
            std::vector<u32> vert_spirv;
            std::vector<u32> frag_spirv;
            shader_reflection_t reflection;
            bool success = false;
        };

        VkShaderModule create_shader_module(const std::vector<u32>& code)
        {
            VkShaderModuleCreateInfo shader_module_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            shader_module_info.pNext = nullptr;
            shader_module_info.codeSize = code.size() * 4;
            shader_module_info.pCode = code.data();

            VkShaderModule module;
            if (vkCreateShaderModule(renderer::ctx::device, &shader_module_info, nullptr, &module) != VK_SUCCESS)
            {
                SMOL_LOG_ERROR("VULKAN", "Failed to create shader module");
                return VK_NULL_HANDLE;
            }

            return module;
        }

        struct traversal_state_t
        {
            slang::TypeLayoutReflection* type_layout;
            std::string prefix;
            u32 base_offset;
        };

        void reflect_struct_members(slang::TypeLayoutReflection* root_type_layout, shader_reflection_t& res, u32 set,
                                    u32 binding)
        {
            std::stack<traversal_state_t> traversal_stack;
            traversal_stack.push({root_type_layout, "", 0});

            while (!traversal_stack.empty())
            {
                traversal_state_t cur = traversal_stack.top();
                traversal_stack.pop();

                u32 field_count = cur.type_layout->getFieldCount();

                for (u32 i = 0; i < field_count; i++)
                {
                    slang::VariableLayoutReflection* field_var = cur.type_layout->getFieldByIndex(i);
                    slang::TypeLayoutReflection* field_type = field_var->getTypeLayout();

                    const char* field_name_cstr = field_var->getName();
                    std::string field_name = cur.prefix + (field_name_cstr ? field_name_cstr : "");

                    u32 relative_offset = (u32)field_var->getOffset();
                    u32 absolute_offset = cur.base_offset + relative_offset;
                    u32 size = (u32)field_type->getSize();

                    slang::TypeReflection::Kind kind = field_type->getKind();

                    if (kind == slang::TypeReflection::Kind::Scalar || kind == slang::TypeReflection::Kind::Vector ||
                        kind == slang::TypeReflection::Kind::Matrix)
                    {
                        shader_uniform_member_t member = {};
                        member.name = field_name;
                        member.offset = absolute_offset;
                        member.size = size;
                        member.set = set;
                        member.binding = binding;

                        res.uniform_members[field_name] = member;
                        SMOL_LOG_INFO("SHADER", "Found member: {}; Offset: {}; Size: {};", field_name, absolute_offset,
                                      size);
                    }
                    else if (kind == slang::TypeReflection::Kind::Struct)
                    {
                        traversal_stack.push({field_type, field_name + ".", absolute_offset});
                    }
                    else if (kind == slang::TypeReflection::Kind::Array)
                    {
                        slang::TypeLayoutReflection* element_type = field_type->getElementTypeLayout();
                        u32 element_count = (u32)field_type->getElementCount();
                        u32 element_stride = (u32)field_type->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);

                        for (u32 j = 0; j < element_count; j++)
                        {
                            std::string array_element_name = field_name + "[" + std::to_string(j) + "]";
                            u32 cur_element_offset = absolute_offset + (j * element_stride);
                            slang::TypeReflection::Kind elem_kind = element_type->getKind();

                            if (elem_kind == slang::TypeReflection::Kind::Scalar ||
                                elem_kind == slang::TypeReflection::Kind::Vector ||
                                elem_kind == slang::TypeReflection::Kind::Matrix)
                            {
                                shader_uniform_member_t member = {};
                                member.name = array_element_name;
                                member.offset = cur_element_offset;
                                member.size = (u32)element_type->getSize();
                                member.set = set;
                                member.binding = binding;

                                res.uniform_members[array_element_name] = member;
                                SMOL_LOG_INFO("SHADER", "Found member: {}; Offset: {}; Size: {};", field_name,
                                              absolute_offset, size);
                            }
                            else if (elem_kind == slang::TypeReflection::Kind::Struct)
                            {
                                traversal_stack.push({element_type, array_element_name + ".", cur_element_offset});
                            }
                        }
                    }
                }
            }
        }

        shader_reflection_t reflect_slang_layout(slang::ProgramLayout* layout)
        {
            shader_reflection_t res;

            u32 param_count = layout->getParameterCount();
            for (u32 i = 0; i < param_count; i++)
            {
                slang::VariableLayoutReflection* var = layout->getParameterByIndex(i);
                slang::TypeLayoutReflection* type = var->getTypeLayout();
                std::string name = std::string(type->getName());
                std::string var_name = std::string(var->getName());

                SMOL_LOG_INFO("SHADER", "Type name: {}", name);

                if (var->getCategory() == slang::ParameterCategory::DescriptorTableSlot)
                {
                    bool is_engine_global = (std::strcmp(var_name.c_str(), "smol") == 0);

                    u32 set_index = var->getBindingSpace();
                    if (set_index == 0 && !is_engine_global) { set_index = 1; }

                    descriptor_binding_t binding_info;
                    binding_info.binding = var->getBindingIndex();
                    binding_info.set = set_index;
                    binding_info.count = 1;

                    slang::TypeReflection::Kind kind = type->getKind();

                    if (kind == slang::TypeReflection::Kind::Array)
                    {
                        binding_info.count = (u32)type->getElementCount();
                        type = type->getElementTypeLayout(); // unwrapping
                        kind = type->getKind();
                    }

                    if (kind == slang::TypeReflection::Kind::Resource)
                    {
                        SlangResourceShape shape = type->getResourceShape();
                        if (shape == SlangResourceShape::SLANG_TEXTURE_2D)
                        {
                            binding_info.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

                            res.bindings[var_name] = binding_info;
                            SMOL_LOG_INFO("SHADER", "Texture: {} --> Set: {}; Binding: {}; Count: {};", name, set_index,
                                          binding_info.binding, binding_info.count);
                        }
                    }
                    else if (kind == slang::TypeReflection::Kind::SamplerState)
                    {
                        binding_info.type = VK_DESCRIPTOR_TYPE_SAMPLER;

                        res.bindings[var_name] = binding_info;
                        SMOL_LOG_INFO("SHADER", "Sampler: {} --> Set: {}; Binding: {}; Count: {};", name, set_index,
                                      binding_info.binding, binding_info.count);
                    }
                    else if (kind == slang::TypeReflection::Kind::ConstantBuffer)
                    {
                        binding_info.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                        res.bindings[var_name] = binding_info;

                        SMOL_LOG_INFO("SHADER", "UBO: {} --> Set: {}; Binding: {};", name, set_index,
                                      binding_info.binding);

                        slang::TypeLayoutReflection* content_type = type->getElementTypeLayout();
                        if (content_type->getKind() == slang::TypeReflection::Kind::Struct)
                        {
                            reflect_struct_members(content_type, res, set_index, binding_info.binding);
                        }
                    }
                }
            }
            return res;
        }

        slang_compilation_res_t compile_slang_to_spirv(const std::string& path)
        {
            slang_compilation_res_t res;

            slang::IGlobalSession* global_session = smol::shader_compiler::get_global_session();

            const char* include_paths[] = {"assets/shaders"};

            slang::SessionDesc session_desc = {};
            session_desc.searchPaths = include_paths;
            session_desc.searchPathCount = 1;

            slang::TargetDesc target_descs[1] = {};
            target_descs[0].format = SLANG_SPIRV;
            target_descs[0].profile = global_session->findProfile("spirv_1_5");
            session_desc.targets = target_descs;
            session_desc.targetCount = 1;

            Slang::ComPtr<slang::ISession> session;
            global_session->createSession(session_desc, session.writeRef());

            Slang::ComPtr<slang::IBlob> diag_blob;
            slang::IModule* module = session->loadModule(path.c_str(), diag_blob.writeRef());

            if (diag_blob) { SMOL_LOG_ERROR("SHADER", "Diagnostics: {}", (const char*)diag_blob->getBufferPointer()); }
            if (!module) { return res; }

            std::vector<slang::IComponentType*> components;
            components.push_back(module);

            Slang::ComPtr<slang::IEntryPoint> vert_entry;
            module->findEntryPointByName("vertexMain", vert_entry.writeRef());
            components.push_back(vert_entry);

            Slang::ComPtr<slang::IEntryPoint> frag_entry;
            module->findEntryPointByName("fragmentMain", frag_entry.writeRef());
            components.push_back(frag_entry);

            Slang::ComPtr<slang::IComponentType> composed_program;
            session->createCompositeComponentType(components.data(), components.size(), composed_program.writeRef());

            Slang::ComPtr<slang::IComponentType> linked_program;
            {
                SlangResult link_res = composed_program->link(linked_program.writeRef(), diag_blob.writeRef());

                if (diag_blob && diag_blob->getBufferSize() > 0)
                {
                    SMOL_LOG_ERROR("SHADER", "Linking of shader program failed: {}",
                                   (const char*)diag_blob->getBufferPointer());
                }

                if (SLANG_FAILED(link_res) || !linked_program) { return res; }
            }

            res.reflection = reflect_slang_layout(linked_program->getLayout());

            Slang::ComPtr<slang::IBlob> kernel_blob;
            linked_program->getEntryPointCode(0, 0, kernel_blob.writeRef(), diag_blob.writeRef());
            if (kernel_blob)
            {
                res.vert_spirv.assign((u32*)kernel_blob->getBufferPointer(),
                                      (u32*)kernel_blob->getBufferPointer() + kernel_blob->getBufferSize() / 4);
            }

            kernel_blob = nullptr;

            linked_program->getEntryPointCode(1, 0, kernel_blob.writeRef(), diag_blob.writeRef());
            if (kernel_blob)
            {
                res.frag_spirv.assign((u32*)kernel_blob->getBufferPointer(),
                                      (u32*)kernel_blob->getBufferPointer() + kernel_blob->getBufferSize() / 4);
            }

            res.success = !res.vert_spirv.empty() && !res.frag_spirv.empty();
            return res;
        }
    } // namespace

    std::optional<shader_t> smol::asset_loader_t<shader_t>::load(const std::string& path)
    {
        slang_compilation_res_t compiled_shader = compile_slang_to_spirv(path);

        if (!compiled_shader.success)
        {
            SMOL_LOG_ERROR("SHADER", "Failed to compile slang file: {}", path);
            return std::nullopt;
        }

        shader_t shader_asset;
        shader_asset.shader_data->reflection = compiled_shader.reflection;

        VkShaderModule vert_mod = create_shader_module(compiled_shader.vert_spirv);
        VkShaderModule frag_mod = create_shader_module(compiled_shader.frag_spirv);

        if (!vert_mod || !frag_mod)
        {
            if (vert_mod) { vkDestroyShaderModule(renderer::ctx::device, vert_mod, nullptr); }
            if (frag_mod) { vkDestroyShaderModule(renderer::ctx::device, frag_mod, nullptr); }
            SMOL_LOG_ERROR("SHADER", "Could not create Vulkan shader module from file at: {}", path);
            return std::nullopt;
        }

        VkPipelineShaderStageCreateInfo vert_stage_info = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        vert_stage_info.pNext = nullptr;
        vert_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vert_stage_info.module = vert_mod;
        vert_stage_info.pName = "main";

        VkPipelineShaderStageCreateInfo frag_stage_info = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        frag_stage_info.pNext = nullptr;
        frag_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        frag_stage_info.module = frag_mod;
        frag_stage_info.pName = "main";

        VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage_info, frag_stage_info};

        VkVertexInputBindingDescription binding_desc = {};
        binding_desc.binding = 0;
        binding_desc.stride = 8 * sizeof(f32);
        binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attribs[3] = {};
        // pos
        attribs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex_t, position)};

        // uvs
        attribs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex_t, normal)};

        // normals
        attribs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex_t, uv)};

        VkPipelineVertexInputStateCreateInfo vertex_input_info = {
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertex_input_info.pNext = nullptr;
        vertex_input_info.vertexBindingDescriptionCount = 1;
        vertex_input_info.pVertexBindingDescriptions = &binding_desc;
        vertex_input_info.vertexAttributeDescriptionCount = 3;
        vertex_input_info.pVertexAttributeDescriptions = attribs;

        VkPipelineInputAssemblyStateCreateInfo input_assembly = {
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        input_assembly.pNext = nullptr;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewport_state = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport_state.pNext = nullptr;
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer = {
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterizer.pNext = nullptr;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // left handed coordinate system
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling_info = {
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling_info.pNext = nullptr;
        multisampling_info.sampleShadingEnable = VK_FALSE;
        multisampling_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth_stencil_info.pNext = nullptr;
        depth_stencil_info.depthTestEnable = VK_TRUE;
        depth_stencil_info.depthWriteEnable = VK_TRUE;
        depth_stencil_info.depthCompareOp = VK_COMPARE_OP_LESS;
        depth_stencil_info.depthBoundsTestEnable = VK_FALSE;
        depth_stencil_info.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState color_blend_attachment_info = {};
        color_blend_attachment_info.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        color_blend_attachment_info.blendEnable =
            VK_FALSE; // enables transparency, off for now (should be decided per shader)

        VkPipelineColorBlendStateCreateInfo color_blend_info = {
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        color_blend_info.pNext = nullptr;
        color_blend_info.logicOpEnable = VK_FALSE;
        color_blend_info.attachmentCount = 1;
        color_blend_info.pAttachments = &color_blend_attachment_info;

        std::vector<VkDynamicState> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic_state = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic_state.pNext = nullptr;
        dynamic_state.dynamicStateCount = static_cast<u32>(dynamic_states.size());
        dynamic_state.pDynamicStates = dynamic_states.data();

        // pipeline layout gen
        VkDescriptorSetLayout material_layout;
        std::vector<VkDescriptorSetLayoutBinding> mat_bindings =
            shader_asset.shader_data->reflection.get_layout_bindings_for_set(1);

        VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout_info.pNext = nullptr;
        layout_info.bindingCount = static_cast<u32>(mat_bindings.size());
        layout_info.pBindings = mat_bindings.data();

        if (vkCreateDescriptorSetLayout(renderer::ctx::device, &layout_info, nullptr, &material_layout) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("SHADER", "Failed to create material descriptor set layout from file at: {}", path);
            return std::nullopt;
        }

        shader_asset.shader_data->material_set_layout = material_layout;

        VkPushConstantRange push_constant = {};
        push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant.offset = 0;
        push_constant.size = sizeof(mat4_t);

        VkDescriptorSetLayout set_layouts[] = {renderer::ctx::global_set_layout, material_layout};

        VkPipelineLayoutCreateInfo pipeline_layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeline_layout_info.pNext = nullptr;
        pipeline_layout_info.setLayoutCount = 2;
        pipeline_layout_info.pSetLayouts = set_layouts;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &push_constant;

        if (vkCreatePipelineLayout(renderer::ctx::device, &pipeline_layout_info, nullptr,
                                   &shader_asset.shader_data->pipeline_layout) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("SHADER", "Failed to create pipeline layout for shader at path: {}", path);
            return std::nullopt;
        }

        VkGraphicsPipelineCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline_info.pNext = nullptr;
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = shader_stages;
        pipeline_info.pVertexInputState = &vertex_input_info;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling_info;
        pipeline_info.pDepthStencilState = &depth_stencil_info;
        pipeline_info.pColorBlendState = &color_blend_info;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = shader_asset.shader_data->pipeline_layout;
        pipeline_info.renderPass = renderer::ctx::render_pass;
        pipeline_info.subpass = 0;
        pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

        if (vkCreateGraphicsPipelines(renderer::ctx::device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                      &shader_asset.shader_data->pipeline) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("SHADER", "Failed to create pipeline for shader at path: {}", path);
            return std::nullopt;
        }

        vkDestroyShaderModule(renderer::ctx::device, vert_mod, nullptr);
        vkDestroyShaderModule(renderer::ctx::device, frag_mod, nullptr);

        return shader_asset;
    }

    u32 shader_reflection_t::get_ubo_size(u32 set, u32 binding) const
    {
        u32 max_offset = 0;
        for (const auto& [name, member] : uniform_members)
        {
            if (member.set == set && member.binding == binding)
            {
                u32 end = member.offset + member.size;
                if (end > max_offset) { max_offset = end; }
            }
        }

        return (max_offset + 15) & ~15; // round up to nearest 16 bytes
    }

    std::vector<VkDescriptorSetLayoutBinding> shader_reflection_t::get_layout_bindings_for_set(u32 set) const
    {
        std::vector<VkDescriptorSetLayoutBinding> res;
        for (const auto& [name, bind] : bindings)
        {
            if (bind.set == set)
            {
                VkDescriptorSetLayoutBinding layout_bind = {};
                layout_bind.binding = bind.binding;
                layout_bind.descriptorCount = bind.count;
                layout_bind.descriptorType = bind.type;
                layout_bind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                res.push_back(layout_bind);
            }
        }

        return res;
    }

    shader_data_t::~shader_data_t()
    {
        if (pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(renderer::ctx::device, pipeline, nullptr); }
        if (pipeline_layout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(renderer::ctx::device, pipeline_layout, nullptr);
        }
        if (material_set_layout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(renderer::ctx::device, material_set_layout, nullptr);
        }
    }
} // namespace smol