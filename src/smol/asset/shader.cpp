#include "shader.h"

#include "smol/asset.h"
#include "smol/color.h"
#include "smol/log.h"
#include "smol/main_thread.h"
#include "smol/rendering/renderer.h"
#include "smol/util.h"

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
            bool success = false;
        };

        slang_compilation_res_t compile_slang_to_glsl(const std::string& path)
        {
            slang_compilation_res_t res;

            Slang::ComPtr<slang::IGlobalSession> global_session;
            slang::createGlobalSession(global_session.writeRef());

            slang::SessionDesc session_desc = {};
            slang::TargetDesc target_desc = {};
            target_desc.format = SLANG_GLSL;
            target_desc.profile = global_session->findProfile("glsl450");
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

            glDeleteShader(vert_shader_id);
            glDeleteShader(frag_shader_id);

            smol::renderer::bind_camera_to_shader(program_id);
        });

        return shader_asset;
    }

    shader_render_data_t::~shader_render_data_t()
    {
        u32 id = program_id;
        if (id != 0)
        {
            smol::main_thread::enqueue([id]() {
                glDeleteProgram(id);
                smol::renderer::unbind_camera_to_shader(id);
            });
        }
    }

    void shader_asset_t::set_uniform(const std::string& name, const uniform_value_t& value) const
    {
        GLint location = glGetUniformLocation(shader_data->program_id, name.c_str());
        if (location == -1)
        {
            SMOL_LOG_ERROR("SHADER", "Could not set uniform with name '{}'", name);
            return;
        }

        std::visit(
            [&](auto&& val) {
                using T = std::decay_t<decltype(val)>;

                if constexpr (std::is_same_v<T, i32>) glUniform1i(location, val);
                if constexpr (std::is_same_v<T, f32>) glUniform1f(location, val);
                if constexpr (std::is_same_v<T, smol::math::vec3_t>) glUniform3fv(location, 1, val.raw());
                if constexpr (std::is_same_v<T, smol::math::vec4_t>) glUniform4fv(location, 1, val.raw());
                if constexpr (std::is_same_v<T, smol::color_t>) glUniform4fv(location, 1, val.data);
                if constexpr (std::is_same_v<T, smol::math::mat4_t>)
                    glUniformMatrix4fv(location, 1, GL_FALSE, val.raw());
            },
            value);
    }

    void shader_asset_t::bind_texture(const std::string& name, u32 tex_id, u32 slot) const
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, tex_id);

        GLint location = glGetUniformLocation(shader_data->program_id, name.c_str());
        if (location == -1)
        {
            SMOL_LOG_ERROR("SHADER", "Could not set sampler with name '{}'", name);
            return;
        }
        glUniform1f(location, slot);
    }
} // namespace smol