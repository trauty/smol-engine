#pragma once

#include "smol/defines.h"

#include <string>
#include <vector>

namespace slang { struct IGlobalSession; }

namespace smol::shader_compiler
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

    SMOL_API std::vector<generated_shader_module_t> generate_uber_shader(const std::string& target_blend_mode,
                                                                         const std::string& output_path);
} // namespace smol::shader_compiler