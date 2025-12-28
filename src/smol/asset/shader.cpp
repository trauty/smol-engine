#include "shader.h"

#include "smol/asset.h"
#include "smol/color.h"
#include "smol/log.h"
#include "smol/main_thread.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/shader_compiler.h"
#include "smol/util.h"

#include <cstddef>
#include <glad/gl.h>
#include <optional>
#include <slang-com-ptr.h>
#include <slang.h>
#include <string>
#include <vector>

namespace smol
{
    namespace
    {
        struct slang_compilation_res_t
        {
            std::string vert_glsl;
            std::string frag_glsl;
            shader_reflection_t reflection;
            bool success = false;
        };

        smol::shader_data_type_e map_slang_type(slang::TypeReflection* type)
        {
            slang::TypeReflection::Kind field_type = type->getKind();
            if (field_type == slang::TypeReflection::Kind::Scalar)
            {
                if (type->getScalarType() == slang::TypeReflection::ScalarType::Float32)
                {
                    return shader_data_type_e::FLOAT;
                }
                if (type->getScalarType() == slang::TypeReflection::ScalarType::Int32)
                {
                    return shader_data_type_e::INT;
                }
            }
            else if (field_type == slang::TypeReflection::Kind::Vector)
            {
                size_t count = type->getElementCount();

                switch (count)
                {
                    case 2: return shader_data_type_e::FLOAT2; break;
                    case 3: return shader_data_type_e::FLOAT3; break;
                    case 4: return shader_data_type_e::FLOAT4; break;
                }
            }
            else if (field_type == slang::TypeReflection::Kind::Matrix)
            {
                u32 rows = type->getRowCount();
                u32 cols = type->getColumnCount();

                if (rows == 4 && cols == 4) { return shader_data_type_e::MAT4; }
            }
            else if (field_type == slang::TypeReflection::Kind::Resource) { return shader_data_type_e::TEXTURE; }

            return shader_data_type_e::UNDEFINED;
        }

        shader_reflection_t reflect_slang_layout(slang::ProgramLayout* layout)
        {
            shader_reflection_t reflection;

            u32 param_count = layout->getParameterCount();
            for (u32 i = 0; i < param_count; i++)
            {
                slang::VariableLayoutReflection* param = layout->getParameterByIndex(i);
                slang::TypeLayoutReflection* type = param->getTypeLayout();

                SMOL_LOG_INFO("SHADER", "Type name: {}", type->getName());

                if (type->getKind() == slang::TypeReflection::Kind::ConstantBuffer)
                {
                    u32 binding_index = param->getBindingIndex();
                    size_t total_size = type->getSize();

                    reflection.ubo_sizes[binding_index] = total_size;

                    u32 field_count = type->getFieldCount();
                    for (u32 f = 0; f < field_count; f++)
                    {
                        slang::VariableLayoutReflection* field = type->getFieldByIndex(f);

                        shader_field_t info;
                        info.name = field->getName();
                        info.type = map_slang_type(field->getTypeLayout()->getType());
                        info.ubo_binding = binding_index;
                        info.offset = field->getOffset();
                        info.size = field->getTypeLayout()->getSize();

                        reflection.material_fields[info.name] = info;
                    }
                }
                else if (type->getKind() == slang::TypeReflection::Kind::Resource)
                {
                    shader_field_t info;
                    info.name = param->getName();
                    info.type = shader_data_type_e::TEXTURE;
                    info.tex_unit = param->getBindingIndex();

                    reflection.material_fields[info.name] = info;
                }
            }
            return reflection;
        }

        slang_compilation_res_t compile_slang_to_glsl(const std::string& path)
        {
            slang_compilation_res_t res;

            slang::IGlobalSession* global_session = smol::shader_compiler::get_global_session();

            slang::SessionDesc session_desc = {};
            slang::TargetDesc target_desc = {};
            target_desc.format = SLANG_GLSL;
            target_desc.profile = global_session->findProfile("glsl_330");
            session_desc.targets = &target_desc;
            session_desc.targetCount = 1;

            Slang::ComPtr<slang::ISession> session;
            global_session->createSession(session_desc, session.writeRef());

            Slang::ComPtr<slang::IBlob> diag_blob;
            slang::IModule* module = session->loadModule(path.c_str(), diag_blob.writeRef());

            if (diag_blob) { SMOL_LOG_ERROR("SHADER", "Diagnostics: {}", (const char*)diag_blob->getBufferPointer()); }
            if (!module) { return res; }

            std::vector<slang::IComponentType*> components;
            components.push_back(module);

            Slang::ComPtr<slang::IEntryPoint> vert_entry;
            module->findEntryPointByName("vertexMain", vert_entry.writeRef());
            components.push_back(vert_entry);

            Slang::ComPtr<slang::IEntryPoint> frag_entry;
            module->findEntryPointByName("fragmentMain", frag_entry.writeRef());
            components.push_back(frag_entry);

            Slang::ComPtr<slang::IComponentType> composed_program;
            session->createCompositeComponentType(components.data(), components.size(), composed_program.writeRef());

            // reflection logic
            slang::ProgramLayout* layout = composed_program->getLayout();
            res.reflection = reflect_slang_layout(layout);

            Slang::ComPtr<slang::IBlob> kernel_blob;
            composed_program->getEntryPointCode(0, 0, kernel_blob.writeRef(), diag_blob.writeRef());
            if (kernel_blob) { res.vert_glsl = std::string((const char*)kernel_blob->getBufferPointer()); }

            kernel_blob = nullptr;

            composed_program->getEntryPointCode(1, 0, kernel_blob.writeRef(), diag_blob.writeRef());
            if (kernel_blob) { res.frag_glsl = std::string((const char*)kernel_blob->getBufferPointer()); }

            res.success = !res.vert_glsl.empty() && !res.frag_glsl.empty();
            return res;
        }

        GLuint compile_glsl_shader(const std::string& src, GLenum type)
        {
            const char* c_src = src.c_str();
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &c_src, nullptr);
            glCompileShader(shader);

            GLint success;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success)
            {
                char log[1024];
                glGetShaderInfoLog(shader, 1024, nullptr, log);
                SMOL_LOG_ERROR("SHADER", "GLSL compile error:\n{}", log);
                return 0;
            }
            return shader;
        }
    } // namespace

    std::optional<shader_asset_t> smol::asset_loader_t<shader_asset_t>::load(const std::string& path)
    {
        slang_compilation_res_t compiled_shader = compile_slang_to_glsl(path);

        if (!compiled_shader.success)
        {
            SMOL_LOG_ERROR("SHADER", "Failed to compile slang file: {}", path);
            return std::nullopt;
        }

        shader_asset_t shader_asset;
        shader_asset.shader_data->reflection = compiled_shader.reflection;

        smol::main_thread::enqueue([shader_data = shader_asset.shader_data,
                                    vert_src = std::move(compiled_shader.vert_glsl),
                                    frag_src = std::move(compiled_shader.frag_glsl)]() {
            GLuint vert_shader_id = compile_glsl_shader(vert_src, GL_VERTEX_SHADER);
            GLuint frag_shader_id = compile_glsl_shader(frag_src, GL_FRAGMENT_SHADER);

            if (!vert_shader_id || !frag_shader_id)
            {
                if (vert_shader_id) { glDeleteShader(vert_shader_id); }
                if (frag_shader_id) { glDeleteShader(frag_shader_id); }
                return;
            }

            GLuint program_id = glCreateProgram();
            glAttachShader(program_id, vert_shader_id);
            glAttachShader(program_id, frag_shader_id);
            glLinkProgram(program_id);

            GLint success;
            glGetProgramiv(program_id, GL_LINK_STATUS, &success);
            if (!success)
            {
                i8 info_log[1024];
                glGetProgramInfoLog(program_id, 1024, nullptr, info_log);
                SMOL_LOG_ERROR("SHADER", "Program linking failed:\n{}", info_log);
                glDeleteProgram(program_id);
            }
            else
            {
                shader_data->program_id = program_id;
            }

            for (auto& [name, field] : shader_data->reflection.material_fields)
            {
                if (field.type == shader_data_type_e::TEXTURE)
                {
                    field.location = glGetUniformLocation(program_id, name.c_str());
                }
            }

            glDeleteShader(vert_shader_id);
            glDeleteShader(frag_shader_id);
        });

        return shader_asset;
    }

    shader_render_data_t::~shader_render_data_t()
    {
        u32 id = program_id;
        if (id != 0)
        {
            smol::main_thread::enqueue([id]() { glDeleteProgram(id); });
        }
    }
} // namespace smol