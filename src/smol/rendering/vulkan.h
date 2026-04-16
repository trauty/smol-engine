#pragma once

// clang-format off
#include "smol/defines.h"
#include <volk.h>
#include <vma/vk_mem_alloc.h>
// clang-format on

namespace smol { enum class descriptor_type_e : u32_t; }

namespace smol::vulkan { VkDescriptorType map_descriptor_type(smol::descriptor_type_e type); }