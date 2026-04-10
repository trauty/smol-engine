
#include "smol-cooker/cache_manager.h"
#include "smol-cooker/mesh_cooker.h"
#include "smol-cooker/shader_cooker.h"
#include "smol-cooker/texture_cooker.h"
#include "smol/log.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#if defined(__SANITIZE_ADDRESS__) || defined(__has_feature)
    #if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
extern "C" const char* __asan_default_options() { return "alloc_dealloc_mismatch=0"; }
    #endif
#endif

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

int main(i32 argc, char** argv)
{
    std::vector<std::string> input_dirs;
    std::vector<std::string> include_dirs;
    std::string output_dir = ".smol";

    for (i32 i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "-i" && i + 1 < argc) { input_dirs.push_back(argv[++i]); }
        else if (arg == "-I" && i + 1 < argc) { include_dirs.push_back(argv[++i]); }
        else if (arg == "-o" && i + 1 < argc) { output_dir = argv[++i]; }
    }

    if (input_dirs.empty())
    {
        SMOL_LOG_ERROR("ASSET_COOKER", "usage: smol-cooker -i <cook_dir> [-I <include_dir>] -o <out_dir> [--game]");
        return 0;
    }

    std::vector<std::string> all_shader_dirs = input_dirs;
    all_shader_dirs.insert(all_shader_dirs.end(), include_dirs.begin(), include_dirs.end());

    smol::log::init();
    smol::cooker::shader::init();

    SMOL_LOG_INFO("ASSET_COOKER", "Cooking assets...");

    smol::cooker::asset_cache_t cache(output_dir + "/cooker_cache.json");
    cache.load();

    std::filesystem::create_directories(output_dir + "/shaders");

    std::vector<std::filesystem::path> core_shader_deps;
    std::unordered_map<std::string, std::vector<std::filesystem::path>> module_deps_by_mode;

    for (const std::string& dir : all_shader_dirs)
    {
        if (!std::filesystem::exists(dir)) { continue; }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".slang")
            {
                std::string filename = entry.path().filename().string();
                if (filename.find("uber_") != std::string::npos) { continue; }

                std::ifstream file(entry.path());
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

                bool is_pipeline =
                    content.find("vertexMain") != std::string::npos || content.find("computeMain") != std::string::npos;
                bool is_module = content.find("ShaderConfig") != std::string::npos &&
                                 content.find("IShaderModule") != std::string::npos;

                if (is_module)
                {
                    std::string blend_mode = "Opaque";
                    if (content.find("\"Cutout\"") != std::string::npos) blend_mode = "Cutout";
                    else if (content.find("\"TransparentAlpha\"") != std::string::npos) blend_mode = "TransparentAlpha";
                    else if (content.find("\"TransparentAdd\"") != std::string::npos) blend_mode = "TransparentAdd";
                    else if (content.find("\"TransparentMult\"") != std::string::npos) blend_mode = "TransparentMult";

                    module_deps_by_mode[blend_mode].push_back(entry.path());
                }
                else if (!is_pipeline) { core_shader_deps.push_back(entry.path()); }
            }
        }
    }

    std::vector<std::string> blend_modes = {
        "Opaque", "Cutout", "TransparentAlpha", "TransparentAdd", "TransparentMult",
    };
    for (const std::string& mode : blend_modes)
    {
        std::string mode_lower = to_lower(mode);
        if (mode == "TransparentAlpha") { mode_lower = "transparent_alpha"; }
        if (mode == "TransparentAdd") { mode_lower = "transparent_add"; }
        if (mode == "TransparentMult") { mode_lower = "transparent_mult"; }

        if (module_deps_by_mode[mode].empty()) { continue; }

        std::string out_path = output_dir + "/shaders/uber_" + mode_lower + ".smolshader";
        std::string pseudo_path = "uber_" + mode_lower + ".slang";

        std::vector<std::filesystem::path> uber_deps = core_shader_deps;
        for (const auto& mod_path : module_deps_by_mode[mode]) { uber_deps.push_back(mod_path); }

        if (cache.needs_cooking(out_path, uber_deps))
        {
            SMOL_LOG_INFO("SHADER_COOKER", "Cooking uber shader: {} -> {}", mode, out_path);

            std::string source = smol::cooker::shader::generate_uber_shader(mode, all_shader_dirs);

            if (source.empty())
            {
                SMOL_LOG_INFO("SHADER_COOKER", "No shader modules found for {}, skipping", mode);
                continue;
            }

            smol::cooker::shader::slang_compilation_res_t res = smol::cooker::shader::compile_slang_to_spirv(
                "uber_" + mode_lower, pseudo_path, source, all_shader_dirs);

            if (res.success)
            {
                smol::cooker::shader::write_smolshader(out_path, res);
                cache.update_cache(out_path, uber_deps);
            }
            else
            {
                SMOL_LOG_ERROR("SHADER_COOKER", "Failed to compile uber shader: {}", mode);
            }
        }
    }

    input_dirs.push_back(output_dir);

    for (const std::string& dir : input_dirs)
    {
        if (!std::filesystem::exists(dir)) { continue; }

        SMOL_LOG_INFO("ASSET_COOKER", "Scanning for assets: {}", dir);

        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
        {
            if (!entry.is_regular_file()) { continue; }

            std::string path = entry.path().generic_string();
            std::string ext = entry.path().extension().string();
            std::string rel_path = std::filesystem::relative(entry.path(), dir).generic_string();
            std::filesystem::path out_path = std::filesystem::path(output_dir) / rel_path;

            if (ext == ".slang" && smol::cooker::shader::is_compilable_pipeline(path))
            {
                if (entry.path().filename().string().find("uber_") != std::string::npos) { continue; }

                out_path.replace_extension(".smolshader");

                std::vector<std::filesystem::path> deps = core_shader_deps;
                deps.push_back(path);

                if (cache.needs_cooking(out_path.generic_string(), deps))
                {
                    smol::cooker::shader::cook_shader(path, out_path.generic_string(), all_shader_dirs);
                    cache.update_cache(out_path.generic_string(), deps);
                }
            }
            else if (ext == ".gltf" || ext == ".glb")
            {
                out_path.replace_extension(".smolmesh");
                if (cache.needs_cooking(out_path.generic_string(), {path}))
                {
                    smol::cooker::mesh::cook_mesh(path, out_path.generic_string());
                    cache.update_cache(out_path.generic_string(), {path});
                }
            }
            else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
            {
                out_path.replace_extension(".ktx2");
                std::string meta_path = path + ".meta";

                std::vector<std::filesystem::path> deps = {path};
                if (std::filesystem::exists(meta_path)) { deps.push_back(meta_path); }

                if (cache.needs_cooking(out_path.generic_string(), deps))
                {
                    smol::cooker::texture::cook_texture(path, out_path.generic_string());

                    if (!std::filesystem::exists(meta_path) && std::filesystem::exists(path + ".meta"))
                    {
                        deps.push_back(path + ".meta");
                    }

                    cache.update_cache(out_path.generic_string(), deps);
                }
            }
        }
    }

    cache.save();

    SMOL_LOG_INFO("ASSET_COOKER", "Cooking finished");

    smol::log::shutdown();

    return 0;
}