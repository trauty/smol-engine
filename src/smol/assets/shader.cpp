#include "shader.h"

#include "smol/defines.h"
#include "smol/log.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_resources.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/shader_compiler.h"
#include "smol/rendering/vulkan.h"
#include "vulkan/vulkan_core.h"

#include <mutex>
#include <optional>
#include <slang-com-ptr.h>
#include <slang.h>
#include <stack>
#include <string>
#include <vector>

namespace smol
{
    namespace
    {
        struct slang_compilation_res_t
        {
            std::vector<u32> vert_spirv;
            std::vector<u32> frag_spirv;
            std::vector<u32> compute_spirv;
            shader_reflection_t reflection;
            bool is_compute = false;

            bool success = false;

            std::string target_pass = "MainForwardPass";
            std::string blend_mode = "Opaque";
            bool depth_write = true;
            bool depth_test = true;

            std::vector<VkFormat> target_formats;
        };

        VkShaderModule create_shader_module(const std::vector<u32>& code)
        {
            VkShaderModuleCreateInfo shader_module_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            shader_module_info.codeSize = code.size() * 4;
            shader_module_info.pCode = code.data();

            VkShaderModule module;
            if (vkCreateShaderModule(renderer::ctx.device, &shader_module_info, nullptr, &module) != VK_SUCCESS)
            {
                SMOL_LOG_ERROR("VULKAN", "Failed to create shader module");
                return VK_NULL_HANDLE;
            }

            return module;
        }

        VkFormat map_alias_to_format(const std::string& alias)
        {
            if (alias == "Swapchain") { return renderer::ctx.swapchain.format; }
            if (alias == "RGBA8_SRGB") { return VK_FORMAT_R8G8B8A8_SRGB; }
            if (alias == "RGBA8_UNORM") { return VK_FORMAT_R8G8B8A8_UNORM; }
            if (alias == "RGBA16_FLOAT") { return VK_FORMAT_R16G16B16A16_SFLOAT; }
            if (alias == "RG16_FLOAT") { return VK_FORMAT_R16G16_SFLOAT; }
            if (alias == "R8_UNORM") { return VK_FORMAT_R8_UNORM; }

            SMOL_LOG_ERROR("SHADER", "Unkown format alias: {}", alias);

            return renderer::ctx.swapchain.format;
        }

        struct traversal_state_t
        {
            slang::TypeLayoutReflection* type_layout;
            std::string prefix;
            u32 base_offset;
        };

        void reflect_material_struct(slang::TypeLayoutReflection* root_type_layout, shader_reflection_t& res)
        {
            res.material_size = static_cast<u32_t>(root_type_layout->getSize());

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
                        shader_member_t member = {};
                        member.name = field_name;
                        member.offset = absolute_offset;
                        member.size = size;

                        res.members[field_name] = member;
                        SMOL_LOG_INFO("SHADER", "Found member: {}; Offset: {}; Size: {};", field_name, absolute_offset,
                                      size);
                    }
                    else if (kind == slang::TypeReflection::Kind::Struct)
                    {
                        traversal_stack.push({field_type, field_name + ".", absolute_offset});
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

                slang::TypeReflection::Kind kind = type->getKind();

                if ((kind == slang::TypeReflection::Kind::Resource &&
                     type->getResourceShape() == SlangResourceShape::SLANG_STRUCTURED_BUFFER) ||
                    kind == slang::TypeReflection::Kind::ConstantBuffer)
                {
                    slang::TypeLayoutReflection* content_type = type->getElementTypeLayout();
                    if (content_type->getKind() == slang::TypeReflection::Kind::Struct)
                    {
                        slang::TypeReflection* struct_type = content_type->getType();

                        bool is_material = false;
                        for (u32 attr_idx = 0; attr_idx < struct_type->getUserAttributeCount(); attr_idx++)
                        {
                            slang::UserAttribute* attr = struct_type->getUserAttributeByIndex(attr_idx);
                            std::string attr_name = attr->getName();

                            if (attr_name == "MaterialReflection" || attr_name == "MaterialReflectionAttribute")
                            {
                                is_material = true;
                                break;
                            }
                        }

                        if (is_material)
                        {
                            SMOL_LOG_INFO("SHADER", "Found material data struct '{}'", type->getName());
                            reflect_material_struct(content_type, res);
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

            Slang::ComPtr<slang::IEntryPoint> comp_entry;
            module->findEntryPointByName("computeMain", comp_entry.writeRef());

            if (comp_entry)
            {
                res.is_compute = true;
                components.push_back(comp_entry);
            }
            else
            {
                Slang::ComPtr<slang::IEntryPoint> vert_entry;
                module->findEntryPointByName("vertexMain", vert_entry.writeRef());
                components.push_back(vert_entry);

                Slang::ComPtr<slang::IEntryPoint> frag_entry;
                module->findEntryPointByName("fragmentMain", frag_entry.writeRef());
                components.push_back(frag_entry);
            }

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

            slang::ProgramLayout* layout = linked_program->getLayout();
            res.reflection = reflect_slang_layout(layout);

            // extraction of render config for pipeline creation
            for (u32 ep_idx = 0; ep_idx < layout->getEntryPointCount(); ep_idx++)
            {
                slang::EntryPointReflection* entry_point = layout->getEntryPointByIndex(ep_idx);
                std::string ep_name = entry_point->getName() ? entry_point->getName() : "";

                if (ep_name == "fragmentMain")
                {
                    slang::VariableLayoutReflection* result_var = entry_point->getResultVarLayout();
                    if (!result_var) { continue; }

                    slang::TypeReflection* result_type = result_var->getTypeLayout()->getType();

                    for (u32 attr_idx = 0; attr_idx < result_type->getUserAttributeCount(); attr_idx++)
                    {
                        slang::UserAttribute* attr_reflection = result_type->getUserAttributeByIndex(attr_idx);
                        std::string attr_name = attr_reflection->getName();

                        if (attr_name == "RenderConfig" || attr_name == "RenderConfigAttribute")
                        {
                            size_t len = 0;
                            if (const char* pass_str = attr_reflection->getArgumentValueString(0, &len))
                            {
                                res.target_pass = std::string(pass_str, len);
                            }
                            if (const char* blend_str = attr_reflection->getArgumentValueString(1, &len))
                            {
                                res.blend_mode = std::string(blend_str, len);
                            }

                            i32 depth_write_val;
                            attr_reflection->getArgumentValueInt(2, &depth_write_val);
                            res.depth_write = depth_write_val != 0;

                            i32 depth_test_val;
                            attr_reflection->getArgumentValueInt(3, &depth_test_val);
                            res.depth_test = depth_test_val != 0;

                            SMOL_LOG_INFO("SHADER", "Found target pass of shader '{}': {}", path, res.target_pass);
                        }
                    }

                    for (u32 field_idx = 0; field_idx < result_type->getFieldCount(); field_idx++)
                    {
                        slang::VariableReflection* field = result_type->getFieldByIndex(field_idx);

                        for (u32 attr_idx = 0; attr_idx < field->getUserAttributeCount(); attr_idx++)
                        {
                            slang::UserAttribute* attr_reflection = field->getUserAttributeByIndex(attr_idx);
                            std::string attr_name = attr_reflection->getName();

                            if (attr_name == "RenderTarget" || attr_name == "RenderTargetAttribute")
                            {
                                size_t len = 0;
                                const char* alias_str = attr_reflection->getArgumentValueString(0, &len);
                                if (alias_str)
                                {
                                    res.target_formats.push_back(map_alias_to_format(std::string(alias_str, len)));
                                }

                                SMOL_LOG_INFO("SHADER",
                                              "Found target format of target '{} - SV_Target{}' of shader '{}': {}",
                                              result_type->getName(), attr_idx, path, alias_str);
                            }
                        }
                    }
                }
            }

            if (res.target_formats.empty()) { res.target_formats.push_back(renderer::ctx.swapchain.format); }

            if (res.is_compute)
            {
                Slang::ComPtr<slang::IBlob> kernel_blob;
                linked_program->getEntryPointCode(0, 0, kernel_blob.writeRef(), diag_blob.writeRef());
                if (kernel_blob)
                {
                    res.compute_spirv.assign((u32*)kernel_blob->getBufferPointer(),
                                             (u32*)kernel_blob->getBufferPointer() + kernel_blob->getBufferSize() / 4);
                }

                res.success = !res.compute_spirv.empty();
            }
            else
            {
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
            }

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
        shader_asset.reflection = compiled_shader.reflection;

        shader_asset.target_pass = compiled_shader.target_pass;
        shader_asset.blend_mode = compiled_shader.blend_mode;
        shader_asset.depth_write = compiled_shader.depth_write;
        shader_asset.depth_test = compiled_shader.depth_test;
        shader_asset.target_formats = compiled_shader.target_formats;
        shader_asset.is_compute = compiled_shader.is_compute;

        VkPipelineVertexInputStateCreateInfo vertex_input_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 0,
            .pVertexBindingDescriptions = nullptr,
            .vertexAttributeDescriptionCount = 0,
            .pVertexAttributeDescriptions = nullptr,
        };

        VkPipelineInputAssemblyStateCreateInfo input_assembly = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };

        VkPipelineViewportStateCreateInfo viewport_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };

        VkPipelineRasterizationStateCreateInfo rasterizer_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .lineWidth = 1.0f,
        };

        VkPipelineMultisampleStateCreateInfo multisampling_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
        };

        VkPipelineDepthStencilStateCreateInfo depth_stencil_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = shader_asset.depth_test ? VK_TRUE : VK_FALSE,
            .depthWriteEnable = shader_asset.depth_write ? VK_TRUE : VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
        };

        VkPipelineColorBlendAttachmentState base_blend = {
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
        };

        if (shader_asset.blend_mode == "Alpha")
        {
            base_blend.blendEnable = VK_TRUE;
            base_blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            base_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        }
        else if (shader_asset.blend_mode == "Additive")
        {
            base_blend.blendEnable = VK_TRUE;
            base_blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            base_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        }

        std::vector<VkPipelineColorBlendAttachmentState> blend_attachments(shader_asset.target_formats.size(),
                                                                           base_blend);

        VkPipelineColorBlendStateCreateInfo color_blend_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .attachmentCount = static_cast<u32_t>(blend_attachments.size()),
            .pAttachments = blend_attachments.empty() ? nullptr : blend_attachments.data(),
        };

        std::vector<VkDynamicState> dynamic_states = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dynamic_state_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = static_cast<u32_t>(dynamic_states.size()),
            .pDynamicStates = dynamic_states.data(),
        };

        VkPushConstantRange push_constant = {
            .stageFlags = static_cast<VkShaderStageFlags>(
                shader_asset.is_compute ? VK_SHADER_STAGE_COMPUTE_BIT
                                        : (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)),
            .offset = 0,
            .size = sizeof(renderer::push_constants_t),
        };

        VkDescriptorSetLayout set_layouts[] = {renderer::res_system.global_layout};

        VkPipelineLayoutCreateInfo pipeline_layout_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = set_layouts,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant,
        };

        if (vkCreatePipelineLayout(renderer::ctx.device, &pipeline_layout_info, nullptr,
                                   &shader_asset.pipeline_layout) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("SHADER", "Failed to create pipeline layout for shader at path: {}", path);
            return std::nullopt;
        }

        if (shader_asset.is_compute)
        {
            VkShaderModule comp_module = create_shader_module(compiled_shader.compute_spirv);

            VkPipelineShaderStageCreateInfo comp_stage_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = comp_module,
                .pName = "main",
            };

            VkComputePipelineCreateInfo comp_pipeline_info = {
                .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .stage = comp_stage_info,
                .layout = shader_asset.pipeline_layout,
            };

            if (vkCreateComputePipelines(renderer::ctx.device, VK_NULL_HANDLE, 1, &comp_pipeline_info, nullptr,
                                         &shader_asset.pipeline) != VK_SUCCESS)
            {
                SMOL_LOG_ERROR("SHADER", "Failed to create compute pipeline for shader at path: {}", path);
            }

            vkDestroyShaderModule(renderer::ctx.device, comp_module, nullptr);
        }
        else
        {
            VkShaderModule vert_mod = create_shader_module(compiled_shader.vert_spirv);
            VkShaderModule frag_mod = create_shader_module(compiled_shader.frag_spirv);

            if (!vert_mod || !frag_mod)
            {
                if (vert_mod) { vkDestroyShaderModule(renderer::ctx.device, vert_mod, nullptr); }
                if (frag_mod) { vkDestroyShaderModule(renderer::ctx.device, frag_mod, nullptr); }
                SMOL_LOG_ERROR("SHADER", "Could not create Vulkan shader module from file at: {}", path);
                return std::nullopt;
            }

            VkPipelineShaderStageCreateInfo vert_stage_info = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            vert_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vert_stage_info.module = vert_mod;
            vert_stage_info.pName = "main";

            VkPipelineShaderStageCreateInfo frag_stage_info = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            frag_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            frag_stage_info.module = frag_mod;
            frag_stage_info.pName = "main";

            VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage_info, frag_stage_info};

            VkPipelineRenderingCreateInfo pipeline_rendering_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                .colorAttachmentCount = static_cast<u32_t>(shader_asset.target_formats.size()),
                .pColorAttachmentFormats =
                    shader_asset.target_formats.empty() ? nullptr : shader_asset.target_formats.data(),
            };

            if (shader_asset.depth_test || shader_asset.depth_write)
            {
                pipeline_rendering_info.depthAttachmentFormat = renderer::ctx.swapchain.depth_format;
            }

            VkGraphicsPipelineCreateInfo pipeline_info = {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .pNext = &pipeline_rendering_info,
                .stageCount = 2,
                .pStages = shader_stages,
                .pVertexInputState = &vertex_input_info,
                .pInputAssemblyState = &input_assembly,
                .pViewportState = &viewport_state,
                .pRasterizationState = &rasterizer_info,
                .pMultisampleState = &multisampling_info,
                .pDepthStencilState = &depth_stencil_info,
                .pColorBlendState = &color_blend_info,
                .pDynamicState = &dynamic_state_info,
                .layout = shader_asset.pipeline_layout,
                .renderPass = VK_NULL_HANDLE,
                .subpass = 0,
                .basePipelineHandle = VK_NULL_HANDLE,
            };

            if (vkCreateGraphicsPipelines(renderer::ctx.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                          &shader_asset.pipeline) != VK_SUCCESS)
            {
                SMOL_LOG_ERROR("SHADER", "Failed to create pipeline for shader at path: {}", path);
            }

            vkDestroyShaderModule(renderer::ctx.device, vert_mod, nullptr);
            vkDestroyShaderModule(renderer::ctx.device, frag_mod, nullptr);
        }

        return shader_asset;
    } // namespace smol

    void asset_loader_t<shader_t>::unload(shader_t& shader)
    {
        if (shader.pipeline == VK_NULL_HANDLE) { return; }

        std::scoped_lock lock(renderer::res_system.deletion_mutex);

        renderer::res_system.deletion_queue.push_back({
            .type = renderer::resource_type_e::PIPELINE,
            .handle = {.pipeline = {shader.pipeline, shader.pipeline_layout}},
            .bindless_id = renderer::BINDLESS_NULL_HANDLE,
            .gpu_timeline_value = renderer::res_system.timeline_value,
        });

        shader.pipeline = VK_NULL_HANDLE;
        shader.pipeline_layout = VK_NULL_HANDLE;
    }
} // namespace smol