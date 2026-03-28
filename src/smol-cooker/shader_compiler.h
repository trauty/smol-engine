#pragma once

#include "smol/assets/shader.h"
#include "smol/defines.h"

#include <string>
#include <vector>

namespace slang { struct IGlobalSession; }

namespace smol::cooker::shader
{
    void init();
    slang::IGlobalSession* get_global_session();

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

    std::vector<generated_shader_module_t> generate_uber_shader(const std::string& target_blend_mode,
                                                                const std::string& output_path);

    slang_compilation_res_t compile_slang_to_spirv(const std::string& path);
    void write_smolshader(const std::string& output_path, const slang_compilation_res_t& res);

    void cook_shader(const std::string& input_path, const std::string& output_path);

    bool is_compilable_pipeline(const std::string& path);
} // namespace smol::cooker::shader