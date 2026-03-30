#include "shader_compiler.h"

#include "smol/assets/shader.h"
#include "smol/assets/shader_format.h"
#include "smol/hash.h"
#include "smol/log.h"
#include "vulkan/vulkan_core.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <slang-com-ptr.h>
#include <slang.h>
#include <string>

namespace smol::cooker::shader
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

    VkFormat map_alias_to_format(const std::string& alias)
    {
        if (alias == "Swapchain") { return VK_FORMAT_UNDEFINED; }
        if (alias == "RGBA8_SRGB") { return VK_FORMAT_R8G8B8A8_SRGB; }
        if (alias == "RGBA8_UNORM") { return VK_FORMAT_R8G8B8A8_UNORM; }
        if (alias == "RGBA16_FLOAT") { return VK_FORMAT_R16G16B16A16_SFLOAT; }
        if (alias == "RG16_FLOAT") { return VK_FORMAT_R16G16_SFLOAT; }
        if (alias == "R8_UNORM") { return VK_FORMAT_R8_UNORM; }

        SMOL_LOG_ERROR("SHADER", "Unkown format alias: {}", alias);

        return VK_FORMAT_UNDEFINED;
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

    std::vector<shader_module_info_t> reflect_slang_layout(slang::ProgramLayout* layout)
    {
        std::vector<shader_module_info_t> res;

        u32_t param_count = layout->getParameterCount();
        for (u32_t i = 0; i < param_count; i++)
        {
            slang::VariableLayoutReflection* param = layout->getParameterByIndex(i);
            if (!param) { continue; }

            slang::TypeLayoutReflection* param_type_layout = param->getTypeLayout();
            if (!param_type_layout) { continue; }

            slang::TypeLayoutReflection* element_layout = param_type_layout->getElementTypeLayout();

            slang::TypeReflection* target_type =
                element_layout ? element_layout->getType() : param_type_layout->getType();
            if (!target_type) { continue; }

            if (target_type->getKind() != slang::TypeReflection::Kind::Struct) { continue; }

            bool is_material = false;
            shader_module_info_t shader_info;
            shader_info.name = target_type->getName() ? target_type->getName() : "Unknown";

            for (u32_t attr_idx = 0; attr_idx < target_type->getUserAttributeCount(); attr_idx++)
            {
                slang::UserAttribute* attr = target_type->getUserAttributeByIndex(attr_idx);
                std::string attr_name = attr->getName();

                if (attr_name == "ShaderConfig" || attr_name == "ShaderConfigAttribute")
                {
                    is_material = true;
                    size_t len = 0;
                    if (const char* pass_str = attr->getArgumentValueString(0, &len))
                    {
                        shader_info.target_pass = std::string(pass_str, len);
                    }

                    if (const char* blend_str = attr->getArgumentValueString(1, &len))
                    {
                        shader_info.blend_mode = std::string(blend_str, len);
                    }

                    i32 dw = 1, dt = 1;
                    attr->getArgumentValueInt(2, &dw);
                    shader_info.depth_write = dw != 0;
                    attr->getArgumentValueInt(3, &dt);
                    shader_info.depth_test = dt != 0;

                    break;
                }
            }

            if (is_material)
            {
                slang::TypeLayoutReflection* struct_layout = layout->getTypeLayout(target_type);
                shader_info.size = struct_layout->getSize();

                for (u32_t field_idx = 0; field_idx < struct_layout->getFieldCount(); field_idx++)
                {
                    slang::VariableLayoutReflection* field_layout = struct_layout->getFieldByIndex(field_idx);
                    shader_member_t member;
                    member.name = field_layout->getVariable()->getName();
                    member.offset = static_cast<u32_t>(field_layout->getOffset());
                    member.size = static_cast<u32_t>(field_layout->getTypeLayout()->getSize());
                    shader_info.members[smol::hash_string(member.name)] = member;
                }

                SMOL_LOG_INFO("SHADER_COOKER", "Discovered shader module: {} (size: {} bytes)", shader_info.name,
                              shader_info.size);
                res.push_back(shader_info);
            }
        }

        return res;
    }

    slang_compilation_res_t compile_slang_to_spirv(const std::string& path)
    {
        slang_compilation_res_t res;

        const char* include_paths[] = {"assets"};

        slang::SessionDesc session_desc = {};
        session_desc.searchPaths = include_paths;
        session_desc.searchPathCount = 1;

        slang::TargetDesc target_descs[1] = {};
        target_descs[0].format = SLANG_SPIRV;
        target_descs[0].profile = global_session->findProfile("sm_6_2");
        session_desc.targets = target_descs;
        session_desc.targetCount = 1;

        Slang::ComPtr<slang::ISession> session;
        global_session->createSession(session_desc, session.writeRef());

        Slang::ComPtr<slang::IBlob> diag_blob;
        slang::IModule* module = session->loadModule(path.c_str(), diag_blob.writeRef());

        if (diag_blob)
        {
            SMOL_LOG_ERROR("SHADER_COOKER", "Diagnostics: {}", (const char*)diag_blob->getBufferPointer());
        }
        if (!module) { return res; }

        std::vector<slang::IComponentType*> components;
        components.push_back(module);

        Slang::ComPtr<slang::IEntryPoint> comp_entry;
        module->findEntryPointByName("computeMain", comp_entry.writeRef());

        if (comp_entry)
        {
            res.is_compute = true;
            components.push_back(comp_entry);
        }
        else
        {
            Slang::ComPtr<slang::IEntryPoint> vert_entry;
            module->findEntryPointByName("vertexMain", vert_entry.writeRef());
            components.push_back(vert_entry);

            Slang::ComPtr<slang::IEntryPoint> frag_entry;
            module->findEntryPointByName("fragmentMain", frag_entry.writeRef());
            components.push_back(frag_entry);
        }

        Slang::ComPtr<slang::IComponentType> composed_program;
        session->createCompositeComponentType(components.data(), components.size(), composed_program.writeRef());

        Slang::ComPtr<slang::IComponentType> linked_program;
        {
            SlangResult link_res = composed_program->link(linked_program.writeRef(), diag_blob.writeRef());

            if (diag_blob && diag_blob->getBufferSize() > 0)
            {
                SMOL_LOG_ERROR("SHADER_COOKER", "Linking of shader program failed: {}",
                               (const char*)diag_blob->getBufferPointer());
            }

            if (SLANG_FAILED(link_res) || !linked_program) { return res; }
        }

        slang::ProgramLayout* layout = linked_program->getLayout();
        std::vector<shader_module_info_t> info = reflect_slang_layout(layout);
        res.shader_types = std::move(info);

        // extraction of render config for pipeline creation
        for (u32 ep_idx = 0; ep_idx < layout->getEntryPointCount(); ep_idx++)
        {
            slang::EntryPointReflection* entry_point = layout->getEntryPointByIndex(ep_idx);
            std::string ep_name = entry_point->getName() ? entry_point->getName() : "";

            if (ep_name == "fragmentMain")
            {
                slang::VariableLayoutReflection* result_var = entry_point->getResultVarLayout();
                if (!result_var) { continue; }

                slang::TypeReflection* result_type = result_var->getTypeLayout()->getType();

                for (u32 field_idx = 0; field_idx < result_type->getFieldCount(); field_idx++)
                {
                    slang::VariableReflection* field = result_type->getFieldByIndex(field_idx);

                    for (u32 attr_idx = 0; attr_idx < field->getUserAttributeCount(); attr_idx++)
                    {
                        slang::UserAttribute* attr_reflection = field->getUserAttributeByIndex(attr_idx);
                        std::string attr_name = attr_reflection->getName();

                        if (attr_name == "RenderTarget" || attr_name == "RenderTargetAttribute")
                        {
                            size_t len = 0;
                            const char* alias_str = attr_reflection->getArgumentValueString(0, &len);
                            if (alias_str)
                            {
                                res.target_formats.push_back(map_alias_to_format(std::string(alias_str, len)));
                            }

                            SMOL_LOG_INFO("SHADER_COOKER",
                                          "Found target format of target '{} - SV_Target{}' of shader '{}': {}",
                                          result_type->getName(), attr_idx, path, alias_str);
                        }
                    }
                }
            }
        }

        if (res.is_compute)
        {
            Slang::ComPtr<slang::IBlob> kernel_blob;
            linked_program->getEntryPointCode(0, 0, kernel_blob.writeRef(), diag_blob.writeRef());
            if (kernel_blob)
            {
                res.compute_spirv.assign((u32*)kernel_blob->getBufferPointer(),
                                         (u32*)kernel_blob->getBufferPointer() + kernel_blob->getBufferSize() / 4);
            }

            res.success = !res.compute_spirv.empty();
        }
        else
        {
            Slang::ComPtr<slang::IBlob> kernel_blob;
            linked_program->getEntryPointCode(0, 0, kernel_blob.writeRef(), diag_blob.writeRef());
            if (kernel_blob)
            {
                res.vert_spirv.assign((u32*)kernel_blob->getBufferPointer(),
                                      (u32*)kernel_blob->getBufferPointer() + kernel_blob->getBufferSize() / 4);
            }

            kernel_blob = nullptr;

            linked_program->getEntryPointCode(1, 0, kernel_blob.writeRef(), diag_blob.writeRef());
            if (kernel_blob)
            {
                res.frag_spirv.assign((u32*)kernel_blob->getBufferPointer(),
                                      (u32*)kernel_blob->getBufferPointer() + kernel_blob->getBufferSize() / 4);
            }

            res.success = !res.vert_spirv.empty() && !res.frag_spirv.empty();
        }

        return res;
    }

    void write_smolshader(const std::string& output_path, const slang_compilation_res_t& res)
    {
        std::filesystem::create_directories(std::filesystem::path(output_path).parent_path());

        std::ofstream out(output_path, std::ios::binary);
        if (!out.is_open())
        {
            SMOL_LOG_ERROR("SHADER_COOKER", "Failed to open output file: {}", output_path);
            return;
        }

        shader_header_t header = {
            .magic = SMOL_SHADER_MAGIC,
            .is_compute = res.is_compute,
            .module_count = static_cast<u32_t>(res.shader_types.size()),
            .vert_spirv_size = static_cast<u32_t>(res.vert_spirv.size()),
            .frag_spirv_size = static_cast<u32_t>(res.frag_spirv.size()),
            .comp_spirv_size = static_cast<u32_t>(res.compute_spirv.size()),
            .target_format_count = static_cast<u32_t>(res.target_formats.size()),
        };
        out.write(reinterpret_cast<const char*>(&header), sizeof(shader_header_t));

        out.write(reinterpret_cast<const char*>(res.target_formats.data()),
                  res.target_formats.size() * sizeof(VkFormat));

        for (const shader_module_info_t& module : res.shader_types)
        {
            shader_module_header_t mod_header = {};
            std::snprintf(mod_header.name, sizeof(mod_header.name), "%s", module.name.c_str());
            std::snprintf(mod_header.target_pass, sizeof(mod_header.target_pass), "%s", module.target_pass.c_str());
            std::snprintf(mod_header.blend_mode, sizeof(mod_header.blend_mode), "%s", module.blend_mode.c_str());
            mod_header.size = module.size;
            mod_header.depth_write = module.depth_write;
            mod_header.depth_test = module.depth_test;
            mod_header.member_count = static_cast<u32_t>(module.members.size());
            out.write(reinterpret_cast<const char*>(&mod_header), sizeof(shader_module_header_t));

            for (const auto& [name_hash, member] : module.members)
            {
                shader_member_header_t member_header = {
                    .name_hash = name_hash,
                    .offset = member.offset,
                    .size = member.size,
                };

                out.write(reinterpret_cast<const char*>(&member_header), sizeof(shader_member_header_t));
            }
        }

        if (!res.vert_spirv.empty())
        {
            out.write(reinterpret_cast<const char*>(res.vert_spirv.data()), res.vert_spirv.size() * 4);
        }

        if (!res.frag_spirv.empty())
        {
            out.write(reinterpret_cast<const char*>(res.frag_spirv.data()), res.frag_spirv.size() * 4);
        }

        if (!res.compute_spirv.empty())
        {
            out.write(reinterpret_cast<const char*>(res.compute_spirv.data()), res.compute_spirv.size() * 4);
        }
    }

    void cook_shader(const std::string& input_path, const std::string& output_path)
    {
        SMOL_LOG_INFO("SHADER_COOKER", "Cooking Shader: {} -> {}", input_path, output_path);

        slang_compilation_res_t res = compile_slang_to_spirv(input_path);

        if (res.success) { write_smolshader(output_path, res); }
        else
        {
            SMOL_LOG_ERROR("SHADER_COOKER", "Failed to cook: {}", input_path);
        }
    }

    bool is_compilable_pipeline(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open()) { return false; }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        return content.find("vertexMain") != std::string::npos || content.find("computeMain") != std::string::npos;
    }
} // namespace smol::cooker::shader