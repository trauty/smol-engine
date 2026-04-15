#include "shader.h"

#include "smol/asset.h"
#include "smol/assets/shader_format.h"
#include "smol/defines.h"
#include "smol/log.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_resources.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/vulkan.h"
#include "smol/vfs.h"
#include "vulkan/vulkan_core.h"

#include <SDL3/SDL_iostream.h>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace smol
{
    namespace
    {
        VkShaderModule create_shader_module(const std::vector<u32_t>& code)
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
    } // namespace

    std::optional<shader_t> smol::asset_loader_t<shader_t>::load(const std::string& path)
    {
        std::string cooked_path = get_cooked_path(path, ".smolshader");

        SDL_IOStream* stream = smol::vfs::open_read(cooked_path);
        if (!stream)
        {
            SMOL_LOG_ERROR("SHADER", "Shader not found: {}", cooked_path);
            return std::nullopt;
        }

        shader_header_t header;
        SDL_ReadIO(stream, &header, sizeof(shader_header_t));

        if (header.magic != SMOL_SHADER_MAGIC)
        {
            SMOL_LOG_ERROR("SHADER", "Invalid .smolshader file: {}", path);
            return std::nullopt;
        }

        shader_t shader;
        shader.is_compute = header.is_compute;
        shader.has_material_data = header.has_material_data;

        shader.target_formats.resize(header.target_format_count);
        SDL_ReadIO(stream, shader.target_formats.data(), header.target_format_count * sizeof(VkFormat));

        for (VkFormat& format : shader.target_formats)
        {
            if (format == VK_FORMAT_UNDEFINED) { format = renderer::ctx.swapchain.format; }
        }

        if (shader.has_material_data)
        {
            shader_module_header_t mod_header;
            SDL_ReadIO(stream, &mod_header, sizeof(shader_module_header_t));

            shader.module = {
                .name = mod_header.name,
                .size = mod_header.size,
                .target_pass = mod_header.target_pass,
                .blend_mode = mod_header.blend_mode,
                .depth_write = mod_header.depth_write,
                .depth_test = mod_header.depth_test,
            };

            for (u32_t j = 0; j < mod_header.member_count; j++)
            {
                shader_member_header_t member_header;
                SDL_ReadIO(stream, &member_header, sizeof(shader_member_header_t));
                shader.module.members[member_header.name_hash] = {"", member_header.offset, member_header.size};
            }
        }

        if (header.descriptor_binding_count > 0)
        {
            shader.descriptor_bindings.resize(header.descriptor_binding_count);
            SDL_ReadIO(stream, shader.descriptor_bindings.data(),
                       header.descriptor_binding_count * sizeof(shader_descriptor_binding_t));
        }

        std::vector<u32_t> vert_spirv(header.vert_spirv_size / 4);
        std::vector<u32_t> frag_spirv(header.frag_spirv_size / 4);
        std::vector<u32_t> comp_spirv(header.comp_spirv_size / 4);

        if (header.vert_spirv_size > 0) { SDL_ReadIO(stream, vert_spirv.data(), header.vert_spirv_size); }

        if (header.frag_spirv_size > 0) { SDL_ReadIO(stream, frag_spirv.data(), header.frag_spirv_size); }

        if (header.comp_spirv_size > 0) { SDL_ReadIO(stream, comp_spirv.data(), header.comp_spirv_size); }

        SDL_CloseIO(stream);

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

        std::string blend_mode = "Opaque";
        bool is_depth_write = true;
        bool is_depth_test = true;

        if (!shader.modules.empty())
        {
            blend_mode = shader.modules[0].blend_mode;
            is_depth_write = shader.modules[0].depth_write;
            is_depth_test = shader.modules[0].depth_test;
        }

        if (blend_mode == "TransparentAlpha")
        {
            base_blend.blendEnable = VK_TRUE;
            base_blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            base_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            base_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            base_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        }
        else if (blend_mode == "TransparentAdd")
        {
            base_blend.blendEnable = VK_TRUE;
            base_blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            base_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            base_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            base_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        }
        else if (blend_mode == "TransparentMult")
        {
            base_blend.blendEnable = VK_TRUE;
            base_blend.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            base_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            base_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
            base_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        }

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
            .cullMode = is_depth_test ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE,
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
            .depthTestEnable = is_depth_test ? VK_TRUE : VK_FALSE,
            .depthWriteEnable = is_depth_write ? VK_TRUE : VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
        };

        std::vector<VkPipelineColorBlendAttachmentState> blend_attachments(shader.target_formats.size(), base_blend);

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
                shader.is_compute ? VK_SHADER_STAGE_COMPUTE_BIT
                                  : (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)),
            .offset = 0,
            .size = sizeof(renderer::push_constants_t),
        };

        VkDescriptorSetLayout set_layouts[] = {renderer::res_system.global_layout, renderer::res_system.frame_layout};

        VkPipelineLayoutCreateInfo pipeline_layout_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 2,
            .pSetLayouts = set_layouts,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant,
        };

        if (vkCreatePipelineLayout(renderer::ctx.device, &pipeline_layout_info, nullptr, &shader.pipeline_layout) !=
            VK_SUCCESS)
        {
            SMOL_LOG_ERROR("SHADER", "Failed to create pipeline layout for shader at path: {}", path);
            return std::nullopt;
        }

        if (shader.is_compute)
        {
            VkShaderModule comp_module = create_shader_module(comp_spirv);

            VkPipelineShaderStageCreateInfo comp_stage_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = comp_module,
                .pName = "main",
            };

            VkComputePipelineCreateInfo comp_pipeline_info = {
                .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .stage = comp_stage_info,
                .layout = shader.pipeline_layout,
            };

            if (vkCreateComputePipelines(renderer::ctx.device, VK_NULL_HANDLE, 1, &comp_pipeline_info, nullptr,
                                         &shader.pipeline) != VK_SUCCESS)
            {
                SMOL_LOG_ERROR("SHADER", "Failed to create compute pipeline for shader at path: {}", path);
            }

            vkDestroyShaderModule(renderer::ctx.device, comp_module, nullptr);
        }
        else
        {
            VkShaderModule vert_mod = create_shader_module(vert_spirv);
            VkShaderModule frag_mod = create_shader_module(frag_spirv);

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
                .colorAttachmentCount = static_cast<u32_t>(shader.target_formats.size()),
                .pColorAttachmentFormats = shader.target_formats.empty() ? nullptr : shader.target_formats.data(),
            };

            if (is_depth_test || is_depth_write)
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
                .layout = shader.pipeline_layout,
                .renderPass = VK_NULL_HANDLE,
                .subpass = 0,
                .basePipelineHandle = VK_NULL_HANDLE,
            };

            if (vkCreateGraphicsPipelines(renderer::ctx.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                          &shader.pipeline) != VK_SUCCESS)
            {
                SMOL_LOG_ERROR("SHADER", "Failed to create pipeline for shader at path: {}", path);
            }

            vkDestroyShaderModule(renderer::ctx.device, vert_mod, nullptr);
            vkDestroyShaderModule(renderer::ctx.device, frag_mod, nullptr);
        }

        return shader;
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