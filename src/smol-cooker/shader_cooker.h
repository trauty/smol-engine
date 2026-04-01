#pragma once

#include "smol/assets/shader.h"
#include "smol/defines.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace smol::cooker::shader
{
    void init();

    struct generated_shader_module_t
    {
        std::string module_name;
        std::string shader_name;
        std::string blend_mode;
        u32_t id;
    };

    struct slang_compilation_res_t
    {
        std::vector<u32> vert_spirv;
        std::vector<u32> frag_spirv;
        std::vector<u32> compute_spirv;
        std::vector<shader_module_info_t> shader_types;
        bool is_compute = false;

        bool success = false;

        std::string target_pass = "MainForwardPass";
        std::string blend_mode = "Opaque";
        bool depth_write = true;
        bool depth_test = true;

        std::vector<VkFormat> target_formats;
    };

    struct shader_timestamps_t
    {
        std::filesystem::file_time_type core_newest = std::filesystem::file_time_type::min();
        std::unordered_map<std::string, std::filesystem::file_time_type> module_newest;
    };

    std::string generate_uber_shader(const std::string& target_blend_mode, const std::vector<std::string>& input_dirs);

    slang_compilation_res_t compile_slang_to_spirv(const std::string& module_name, const std::string& file_path,
                                                   const std::string& source_code,
                                                   const std::vector<std::string>& input_dirs);
    void write_smolshader(const std::string& output_path, const slang_compilation_res_t& res);

    void cook_shader(const std::string& input_path, const std::string& output_path,
                     const std::vector<std::string>& input_dirs);

    bool is_compilable_pipeline(const std::string& path);

    shader_timestamps_t scan_shader_deps(const std::vector<std::string>& input_dirs);
} // namespace smol::cooker::shader