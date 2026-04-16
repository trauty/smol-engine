#pragma once

#include "smol/assets/shader.h"
#include "smol/assets/shader_format.h"
#include "smol/defines.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace smol::cooker::shader
{
    void init();

    struct slang_compilation_res_t
    {
        std::vector<u32> vert_spirv;
        std::vector<u32> frag_spirv;
        std::vector<u32> compute_spirv;

        std::vector<shader_module_info_t> shader_types;
        std::vector<VkFormat> target_formats;
        std::vector<shader_descriptor_binding_t> descriptor_bindings;

        bool is_compute = false;
        bool success = false;
    };

    slang_compilation_res_t compile_slang_to_spirv(const std::string& module_name, const std::string& file_path,
                                                   const std::string& source_code,
                                                   const std::vector<std::string>& input_dirs);
    void write_smolshader(const std::string& output_path, const slang_compilation_res_t& res);

    void cook_shader(const std::string& input_path, const std::string& output_path,
                     const std::vector<std::string>& input_dirs);

    bool is_compilable_pipeline(const std::string& path);
} // namespace smol::cooker::shader