#include "shader_compiler.h"

#include "smol/log.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <slang-com-ptr.h>
#include <slang.h>
#include <string>

namespace smol::shader_compiler
{
    namespace { Slang::ComPtr<slang::IGlobalSession> global_session; }

    slang::IGlobalSession* get_global_session() { return global_session.get(); }

    void init()
    {
        SlangGlobalSessionDesc global_desc = {};
        global_desc.enableGLSL = true;
        SMOL_LOG_INFO("SHADER", "Slang global instance: {}",
                      slang::createGlobalSession(&global_desc, global_session.writeRef()));
    }

    std::string extract_struct_name(const std::string& code)
    {
        size_t config_pos = code.find("ShaderConfig");
        if (config_pos == std::string::npos) { return ""; }

        size_t pos = code.find("struct ", config_pos);
        if (pos == std::string::npos) { return ""; }

        pos += 7;

        while (pos < code.size() && std::isspace(code[pos])) { pos++; }
        size_t start = pos;
        while (pos < code.size() && (std::isalnum(code[pos]) || code[pos] == '_')) { pos++; }

        return code.substr(start, pos - start);
    }

    std::vector<generated_shader_module_t> generate_uber_shader(const std::string& target_blend_mode,
                                                                const std::string& output_path)
    {
        std::vector<generated_shader_module_t> shaders;
        u32_t next_id = 0;

        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator("assets"))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".slang") { continue; }

            SMOL_LOG_INFO("SHADER_COMPILER", "Found shader: {}", entry.path().string());

            std::string filename = entry.path().stem().string();
            if (filename.find("uber_") != std::string::npos) { continue; }

            std::filesystem::path rel_path = std::filesystem::relative(entry.path(), "assets");
            std::string module_name = rel_path.replace_extension("").generic_string();
            std::replace(module_name.begin(), module_name.end(), '/', '.');

            std::ifstream file(entry.path());
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            if (content.find("IShaderModule") != std::string::npos && content.find("ShaderConfig") != std::string::npos)
            {
                generated_shader_module_t mod;
                mod.module_name = module_name;
                mod.shader_name = extract_struct_name(content);

                if (content.find("\"Opaque\"") != std::string::npos) { mod.blend_mode = "Opaque"; }
                else if (content.find("\"Cutout\"") != std::string::npos) { mod.blend_mode = "Cutout"; }
                else if (content.find("\"TransparentAlpha\"") != std::string::npos)
                {
                    mod.blend_mode = "TransparentAlpha";
                }
                else if (content.find("\"TransparentAdd\"") != std::string::npos) { mod.blend_mode = "TransparentAdd"; }
                else if (content.find("\"TransparentMult\"") != std::string::npos)
                {
                    mod.blend_mode = "TransparentMult";
                }
                else
                {
                    mod.blend_mode = "Opaque";
                }

                if (mod.blend_mode == target_blend_mode)
                {
                    mod.id = next_id++;
                    shaders.push_back(mod);
                }
            }
        }

        if (shaders.empty()) { return {}; }

        std::ofstream out(output_path);
        out << "import smol_globals;\nimport shader_interface;\n\n";

        for (generated_shader_module_t& mod : shaders) { out << "import " << mod.module_name << ";\n"; }

        out << "\npublic struct VertexOut\n{\n";
        out << "    public float4 position : SV_Position;\n    public float3 world_pos;\n    public float3 normal;\n   "
               " public float2 uv;\n    public uint draw_id;\n};\n\n";

        out << "[shader(\"vertex\")]\nVertexOut vertexMain(uint vertex_id: SV_VertexID, uint instance_id: "
               "SV_InstanceID, uint base_instance: SV_StartInstanceLocation)\n{\n";
        out << "    uint actual_instance_id = instance_id + base_instance;\n    ObjectData obj = "
               "getObjectData(actual_instance_id);\n    GlobalData globals = getGlobalData();\n";
        out << "    Vertex vertex = getIndexedVertex(obj.vertex_buffer_id, obj.index_buffer_id, vertex_id);\n    "
               "float3 offset = float3(0.0f, 0.0f, 0.0f);\n\n";

        out << "    switch (obj.material_type)\n    {\n";
        for (generated_shader_module_t& mod : shaders)
        {
            out << "    case " << mod.id << ": {\n        " << mod.shader_name
                << " m = getBuffer(pc.material_buffer_id).Load<" << mod.shader_name << ">(obj.material_offset);\n";
            out << "        offset = m.vertex(vertex.position, vertex.normal, globals.time);\n        break; }\n";
        }
        out << "    default: break;\n    }\n\n";

        out << "    float3 local_pos = vertex.position + offset;\n    float4 world_pos = mul(float4(local_pos, 1.0f), "
               "obj.model_matrix);\n";
        out << "    VertexOut out_v;\n    out_v.position = mul(world_pos, globals.view_proj);\n    out_v.world_pos = "
               "world_pos.xyz;\n";
        out << "    float3x3 normal_mat = float3x3(obj.normal_matrix[0].xyz, obj.normal_matrix[1].xyz, "
               "obj.normal_matrix[2].xyz);\n";
        out << "    out_v.normal = normalize(mul(vertex.normal, normal_mat));\n    out_v.uv = vertex.uv;\n    "
               "out_v.draw_id = actual_instance_id;\n    return out_v;\n}\n\n";

        out << "[RenderTargetsConfig]\nstruct FragmentOut\n{\n    [RenderTarget(\"RGBA16_FLOAT\")]\n    float4 color : "
               "SV_Target0;\n};\n\n";
        out << "[shader(\"fragment\")]\nFragmentOut fragmentMain(VertexOut in_f)\n{\n";
        out << "    ObjectData obj = getObjectData(in_f.draw_id);\n    float4 color = float4(1.0f, 0.0f, 1.0f, "
               "1.0f);\n\n";

        out << "    switch (obj.material_type)\n    {\n";
        for (generated_shader_module_t& mod : shaders)
        {
            out << "    case " << mod.id << ": {\n        " << mod.shader_name
                << " m = getBuffer(pc.material_buffer_id).Load<" << mod.shader_name << ">(obj.material_offset);\n";
            out << "        color = m.fragment(in_f.uv, in_f.world_pos, in_f.normal);\n        break; }\n";
        }
        out << "    default: break;\n    }\n\n";
        out << "    FragmentOut out_frag;\n    out_frag.color = color;\n    return out_frag;\n}\n\n";

        out << "// --- REFLECTION HOOKS ---\n";
        for (generated_shader_module_t& mod : shaders)
        {
            out << "StructuredBuffer<" << mod.shader_name << "> _reflect_" << mod.shader_name << ";\n";
        }

        return shaders;
    }
} // namespace smol::shader_compiler