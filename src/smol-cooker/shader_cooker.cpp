#include "shader_cooker.h"

#include "smol/assets/shader.h"
#include "smol/assets/shader_format.h"
#include "smol/hash.h"
#include "smol/log.h"

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
#include <system_error>
#include <vector>

namespace smol::cooker::shader
{
    namespace { Slang::ComPtr<slang::IGlobalSession> global_session; }

    void init()
    {
        SlangGlobalSessionDesc global_desc = {};
        SMOL_LOG_INFO("SHADER_COOKER", "Slang global instance: {}",
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

    smol::descriptor_type_e map_slang_type_to_descriptor(slang::TypeReflection* type)
    {
        slang::TypeReflection::Kind kind = type->getKind();
        if (kind == slang::TypeReflection::Kind::Resource)
        {
            SlangResourceShape shape = type->getResourceShape();
            SlangResourceAccess access = type->getResourceAccess();

            if (shape == SLANG_STRUCTURED_BUFFER || shape == SLANG_BYTE_ADDRESS_BUFFER)
            {
                return descriptor_type_e::STORAGE_BUFFER;
            }
            if (shape == SLANG_TEXTURE_2D)
            {
                return access == SLANG_RESOURCE_ACCESS_READ_WRITE ? descriptor_type_e::STORAGE_IMAGE
                                                                  : descriptor_type_e::SAMPLED_IMAGE;
            }
        }
        if (kind == slang::TypeReflection::Kind::SamplerState) { return descriptor_type_e::SAMPLER; }
        if (kind == slang::TypeReflection::Kind::ConstantBuffer) { return descriptor_type_e::UNIFORM_BUFFER; }

        return descriptor_type_e::STORAGE_BUFFER;
    }

    void reflect_descriptors(slang::ProgramLayout* layout, slang_compilation_res_t& res)
    {
        u32_t param_count = layout->getParameterCount();
        for (u32_t i = 0; i < param_count; i++)
        {
            slang::VariableLayoutReflection* param = layout->getParameterByIndex(i);
            if (!param) { continue; }

            u32_t set = param->getBindingSpace();

            if (set < 2) { continue; } // ignore bindless and globals sets

            shader_descriptor_binding_t binding;
            binding.name_hash = smol::hash_string(param->getName());
            binding.set = set;
            binding.binding = param->getBindingIndex();
            binding.count = 1;

            slang::TypeLayoutReflection* type_layout = param->getTypeLayout();
            binding.type = map_slang_type_to_descriptor(type_layout->getType());

            res.descriptor_bindings.push_back(binding);

            SMOL_LOG_INFO("SHADER_COOKER", "Detected custom descriptor: Set {}; Binding {}; Type {};", set,
                          binding.binding, (u32_t)binding.type);
        }
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

    slang_compilation_res_t compile_slang_to_spirv(const std::string& module_name, const std::string& file_path,
                                                   const std::string& source_code,
                                                   const std::vector<std::string>& input_dirs)
    {
        slang_compilation_res_t res;

        std::vector<std::string> actual_include_paths;
        for (const std::string& dir : input_dirs)
        {
            actual_include_paths.push_back(dir.c_str());
            if (!std::filesystem::exists(dir)) { continue; }

            for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".slang")
                {
                    std::string parent_dir = entry.path().parent_path().generic_string();

                    if (std::find(actual_include_paths.begin(), actual_include_paths.end(), parent_dir) ==
                        actual_include_paths.end())
                    {
                        actual_include_paths.push_back(parent_dir.c_str());
                    }
                }
            }
        }

        std::vector<const char*> include_paths;
        for (const std::string& dir : actual_include_paths) { include_paths.push_back(dir.c_str()); }

        slang::SessionDesc session_desc = {};
        session_desc.searchPaths = include_paths.data();
        session_desc.searchPathCount = include_paths.size();

        std::vector<slang::CompilerOptionEntry> compiler_options;

        auto add_int_option = [&](slang::CompilerOptionName name, int32_t val)
        {
            slang::CompilerOptionEntry entry = {};
            entry.name = name;
            entry.value.kind = slang::CompilerOptionValueKind::Int;
            entry.value.intValue0 = val;
            compiler_options.push_back(entry);
        };

        add_int_option(slang::CompilerOptionName::EmitSpirvDirectly, 1);
        add_int_option(slang::CompilerOptionName::Optimization, 3);

        SlangCapabilityID spirv15_cap = global_session->findCapability("spirv_1_5");
        if (spirv15_cap != 0) { add_int_option(slang::CompilerOptionName::Capability, spirv15_cap); }

        slang::TargetDesc target_descs[1] = {};
        target_descs[0].format = SLANG_SPIRV;
        target_descs[0].profile = global_session->findProfile("sm_6_6");
        target_descs[0].compilerOptionEntryCount = compiler_options.size();
        target_descs[0].compilerOptionEntries = compiler_options.data();
        session_desc.targets = target_descs;
        session_desc.targetCount = 1;

        Slang::ComPtr<slang::ISession> session;
        global_session->createSession(session_desc, session.writeRef());

        Slang::ComPtr<slang::IBlob> diag_blob;

        slang::IModule* module = session->loadModuleFromSourceString(module_name.c_str(), file_path.c_str(),
                                                                     source_code.c_str(), diag_blob.writeRef());

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
        res.shader_types = reflect_slang_layout(layout);
        reflect_descriptors(layout, res);

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
                                          result_type->getName(), attr_idx, file_path, alias_str);
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
            .has_material_data = !res.shader_types.empty(),
            .vert_spirv_size = static_cast<u32_t>(res.vert_spirv.size()),
            .frag_spirv_size = static_cast<u32_t>(res.frag_spirv.size()),
            .comp_spirv_size = static_cast<u32_t>(res.compute_spirv.size()),
            .target_format_count = static_cast<u32_t>(res.target_formats.size()),
            .descriptor_binding_count = static_cast<u32_t>(res.descriptor_bindings.size()),
        };
        out.write(reinterpret_cast<const char*>(&header), sizeof(shader_header_t));

        out.write(reinterpret_cast<const char*>(res.target_formats.data()),
                  res.target_formats.size() * sizeof(VkFormat));

        if (!res.shader_types.empty())
        {
            shader_module_info_t module = res.shader_types[0];

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

        if (!res.descriptor_bindings.empty())
        {
            out.write(reinterpret_cast<const char*>(res.descriptor_bindings.data()),
                      res.descriptor_bindings.size() * sizeof(shader_descriptor_binding_t));
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

    void cook_shader(const std::string& input_path, const std::string& output_path,
                     const std::vector<std::string>& input_dirs)
    {
        SMOL_LOG_INFO("SHADER_COOKER", "Cooking Shader: {} -> {}", input_path, output_path);

        std::ifstream file(input_path, std::ios::binary);
        if (!file.is_open())
        {
            SMOL_LOG_ERROR("SHADER_COOKER", "Failed to open shader file: {}", input_path);
            return;
        }

        std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string module_name = std::filesystem::path(input_path).stem().string();

        std::string struct_name = extract_struct_name(source);
        if (!struct_name.empty()) { source += "\nStructuredBuffer<" + struct_name + "> _reflection;\n"; }

        for (const std::string& dir : input_dirs)
        {
            std::error_code ec;
            std::filesystem::path rel_path = std::filesystem::relative(input_path, dir, ec);

            if (!ec && !rel_path.empty() && rel_path.string().find("..") == std::string::npos)
            {
                module_name = rel_path.replace_extension("").generic_string();
                std::replace(module_name.begin(), module_name.end(), '/', '.');
                break;
            }
        }

        slang_compilation_res_t res = compile_slang_to_spirv(module_name, input_path, source, input_dirs);

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