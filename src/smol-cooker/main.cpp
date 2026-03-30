
#include "smol-cooker/mesh_cooker.h"
#include "smol-cooker/shader_compiler.h"
#include "smol-cooker/texture_cooker.h"
#include "smol/log.h"

#include <filesystem>

#if defined(__SANITIZE_ADDRESS__) || defined(__has_feature)
    #if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
extern "C" const char* __asan_default_options() { return "alloc_dealloc_mismatch=0"; }
    #endif
#endif

std::string redirect_output_path(const std::string& path)
{
    std::string new_path = path;
    if (path.starts_with("assets/")) { new_path.replace(0, 7, ".smol/"); }
    else if (path.starts_with("assets\\")) { new_path.replace(0, 7, ".smol\\"); }
    return new_path;
}

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

    SMOL_LOG_INFO("COOKER", "Scanning for assets...");

    for (const auto& entry : std::filesystem::recursive_directory_iterator("assets"))
    {
        if (!entry.is_regular_file()) { continue; }
        if (entry.path().extension() == ".slang")
        {
            std::string input_path = entry.path().generic_string();

            if (!smol::cooker::shader::is_compilable_pipeline(input_path)) { continue; }

            std::string output_path = redirect_output_path(input_path);

            std::filesystem::path out_p(output_path);
            out_p.replace_extension(".smolshader");

            smol::cooker::shader::cook_shader(input_path, out_p.generic_string());
        }
        else if (entry.path().extension() == ".gltf" || entry.path().extension() == ".glb")
        {
            std::string input_path = entry.path().generic_string();
            std::string output_path = redirect_output_path(input_path);

            std::filesystem::path out_p(output_path);
            out_p.replace_extension(".smolmesh");

            smol::cooker::mesh::cook_mesh(input_path, out_p.generic_string());
        }
        else if (entry.path().extension() == ".png" || entry.path().extension() == ".jpg" ||
                 entry.path().extension() == ".jpeg")
        {
            std::string input_path = entry.path().generic_string();
            std::string output_path = redirect_output_path(input_path);

            std::filesystem::path out_p(output_path);
            out_p.replace_extension(".ktx2");

            smol::cooker::texture::cook_texture(input_path, out_p.generic_string());
        }
    }

    SMOL_LOG_INFO("COOKER", "Cooking finished");

    smol::log::shutdown();

    return 0;
}