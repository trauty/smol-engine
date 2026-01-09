#pragma once
#include "smol/asset/shader.h"
#include "smol/asset/texture.h"
#include "smol/log.h"

#include <cstring>
#include <unordered_map>
#include <vulkan/vulkan_core.h>

namespace smol
{
    struct ubo_resource_t
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped_data = nullptr;
        size_t size = 0;
    };

    struct material_t
    {
        smol::shader_asset_t shader;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

        std::unordered_map<u32, ubo_resource_t> ubos;

        ~material_t();

        void init(const smol::shader_asset_t& shader);
        void set_data(const std::string& block_name, const void* data, size_t size);
        void set_texture(const std::string& name, const smol::texture_asset_t& texture);

        template<typename T>
        void set_parameter(const std::string& name, const T& value)
        {
            if (!shader.ready()) { return; }

            const std::unordered_map<std::string, shader_uniform_member_t>& members =
                shader.shader_data->reflection.uniform_members;

            auto it = members.find(name);
            if (it == members.end())
            {
                SMOL_LOG_WARN("MATERIAL", "Parameter not found in shader bindings: {}", name);
                return;
            }

            const shader_uniform_member_t& member = it->second;
            auto ubo_it = ubos.find(member.binding);
            if (ubo_it == ubos.end()) { return; }

            ubo_resource_t& ubo = ubo_it->second;

            if (member.offset + sizeof(T) > ubo.size) { return; } // should probably warn here

            std::memcpy(static_cast<u8*>(ubo.mapped_data) + member.offset, &value, sizeof(T));
        }
    };
} // namespace smol