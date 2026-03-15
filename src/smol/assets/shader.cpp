#include "shader.h"

#include "smol/log.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_resources.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/shader_compiler.h"
#include "smol/rendering/vulkan.h"

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
            shader_reflection_t reflection;
            bool success = false;
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
                std::string var_name = var->getName() ? std::string(var->getName()) : "";

                SMOL_LOG_INFO("SHADER", "Type name: {}", var_name);

                if (var_name != "pc" && !var_name.empty())
                {
                    slang::TypeReflection::Kind kind = type->getKind();

                    if ((kind == slang::TypeReflection::Kind::Resource &&
                         type->getResourceShape() == SlangResourceShape::SLANG_STRUCTURED_BUFFER) ||
                        kind == slang::TypeReflection::Kind::ConstantBuffer)
                    {
                        slang::TypeLayoutReflection* content_type = type->getElementTypeLayout();
                        if (content_type->getKind() == slang::TypeReflection::Kind::Struct)
                        {
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
        shader_asset.reflection = compiled_shader.reflection;

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
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
        };

        VkPipelineColorBlendAttachmentState color_blend_attachment_info = {
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
        };

        VkPipelineColorBlendStateCreateInfo color_blend_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .attachmentCount = 1,
            .pAttachments = &color_blend_attachment_info,
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
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(u32_t) * 3,
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

        VkFormat color_format = renderer::ctx.swapchain.format;
        VkFormat depth_format = renderer::ctx.swapchain.depth_format;

        VkPipelineRenderingCreateInfo pipeline_rendering_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &color_format,
            .depthAttachmentFormat = depth_format,
        };

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
            return std::nullopt;
        }

        vkDestroyShaderModule(renderer::ctx.device, vert_mod, nullptr);
        vkDestroyShaderModule(renderer::ctx.device, frag_mod, nullptr);

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