#pragma once
#include "smol/asset.h"
#include "smol/assets/shader.h"
#include "smol/assets/texture.h"
#include "smol/log.h"

#include <cstring>
#include <unordered_map>
#include <utility>
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
        asset_t<shader_t> shader;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

        std::unordered_map<u32, ubo_resource_t> ubos;

        std::unordered_map<std::string, asset_t<texture_t>> texture_bindings;

        material_t() = default;

        material_t(material_t&& other) noexcept
            : shader(std::move(other.shader)), descriptor_set(other.descriptor_set), ubos(std::move(other.ubos))
        {
            other.descriptor_set = VK_NULL_HANDLE;
        }

        material_t& operator=(material_t&& other) noexcept
        {
            if (this != &other)
            {
                this->~material_t();
                new (this) material_t(std::move(other));
            }

            return *this;
        }

        ~material_t();

        void set_shader(asset_t<shader_t> s) { shader = s; }
        bool try_build_resources();
        void set_data(const std::string& block_name, const void* data, size_t size);
        void set_texture(const std::string& tex_name, asset_t<texture_t> texture);

        template<typename T>
        void set_parameter(const std::string& name, const T& value)
        {
            if (!shader->ready()) { return; }

            const std::unordered_map<std::string, shader_uniform_member_t>& members =
                shader.get()->shader_data->reflection.uniform_members;

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