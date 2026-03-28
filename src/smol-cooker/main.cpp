
#include "smol-cooker/shader_compiler.h"
#include "smol/log.h"

#include <filesystem>
int main()
{
    smol::log::init();
    smol::cooker::shader::init();

    SMOL_LOG_INFO("ASSET_COOKER", "Cooking assets...");

    smol::cooker::shader::generate_uber_shader("Opaque", "assets/shaders/uber_opaque.slang");
    smol::cooker::shader::generate_uber_shader("Cutout", "assets/shaders/uber_cutout.slang");
    smol::cooker::shader::generate_uber_shader("TransparentAlpha", "assets/shaders/uber_transparent_alpha.slang");
    smol::cooker::shader::generate_uber_shader("TransparentAdd", "assets/shaders/uber_transparent_add.slang");
    smol::cooker::shader::generate_uber_shader("TransparentMult", "assets/shaders/uber_transparent_mult.slang");

    SMOL_LOG_INFO("COOKER", "Scanning for shaders...");

    for (const auto& entry : std::filesystem::recursive_directory_iterator("assets/shaders"))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".slang")
        {
            std::string input_path = entry.path().generic_string();

            if (!smol::cooker::shader::is_compilable_pipeline(input_path)) { continue; }

            std::string output_path = input_path;
            if (output_path.starts_with("assets/")) { output_path.replace(0, 7, ".smol/"); }
            else if (output_path.starts_with("assets\\")) { output_path.replace(0, 7, ".smol\\"); }

            std::filesystem::path out_p(output_path);
            out_p.replace_extension(".smolshader");

            smol::cooker::shader::cook_shader(input_path, out_p.generic_string());
        }
    }

    SMOL_LOG_INFO("COOKER", "Cooking finished");

    smol::log::shutdown();

    return 0;
}