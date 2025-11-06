#include "shader.h"

#include "smol/color.h"
#include "smol/log.h"
#include "smol/main_thread.h"
#include "smol/rendering/renderer.h"
#include "smol/util.h"
#include "texture.h"

#include <glad/gl.h>

namespace smol::asset
{
    shader_asset_t::shader_asset_t(const std::string& packed_path)
    {
        size_t delim = packed_path.find('|');
        if (delim == std::string::npos)
        {
            SMOL_LOG_ERROR("SHADER", "Invalid packed shader path: {}", packed_path);
            return;
        }

        std::string vert_src = smol::util::get_file_content(packed_path.substr(0, delim));
        std::string frag_src = smol::util::get_file_content(packed_path.substr(delim + 1));

        smol::main_thread::enqueue([this, vert_src, frag_src]()
                                   {
            GLuint vert_shader_id = 0, frag_shader_id = 0;
            if (compile_shader(vert_src, GL_VERTEX_SHADER, vert_shader_id) != SMOL_SUCCESS) return;
            if (compile_shader(frag_src, GL_FRAGMENT_SHADER, frag_shader_id) != SMOL_SUCCESS) return;

            program_id = glCreateProgram();
            glAttachShader(program_id, vert_shader_id);
            glAttachShader(program_id, frag_shader_id);
            glLinkProgram(program_id);

            GLint success;
            glGetProgramiv(program_id, GL_LINK_STATUS, &success);
            if (!success)
            {
                i8 info_log[512];
                glGetProgramInfoLog(program_id, 512, nullptr, info_log);
                SMOL_LOG_ERROR("SHADER", "Program linking failed:\n{}", info_log);
                program_id = 0;
            }

            glDeleteShader(vert_shader_id);
            glDeleteShader(frag_shader_id);

            smol::renderer::bind_camera_to_shader(program_id); });
    }

    shader_asset_t::~shader_asset_t()
    {
        smol::main_thread::enqueue([this]()
                                   {
            if (program_id) 
            {
                glDeleteProgram(program_id);
                smol::renderer::unbind_camera_to_shader(program_id);
            } });
    }

    smol_result_e shader_asset_t::compile_shader(const std::string& src, GLenum type, GLuint& out_shader)
    {
        const char* src_cstr = src.c_str();
        out_shader = glCreateShader(type);
        glShaderSource(out_shader, 1, &src_cstr, nullptr);
        glCompileShader(out_shader);

        GLint success;
        glGetShaderiv(out_shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            i8 info_log[512];
            glGetShaderInfoLog(out_shader, 512, nullptr, info_log);
            SMOL_LOG_ERROR("SHADER", "Shader compilation ({}) of type '{}' failed:\n{}", src, type, info_log);
            return SMOL_FAILURE;
        }

        return SMOL_SUCCESS;
    }

    // only call from main thread
    void shader_asset_t::bind()
    {
        glUseProgram(program_id);
    }

    // only call from main thread
    void shader_asset_t::set_uniform(const std::string& name, const uniform_value_t& value) const
    {
        GLint location = glGetUniformLocation(program_id, name.c_str());
        if (location == -1)
        {
            SMOL_LOG_ERROR("SHADER", "Could not set uniform with name '{}'", name);
            return;
        }

        std::visit([&](auto&& val)
                   {
            using T = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<T, i32>) glUniform1i(location, val);
            if constexpr (std::is_same_v<T, f32>) glUniform1f(location, val);
            if constexpr (std::is_same_v<T, smol::math::vec3_t>) glUniform3fv(location, 1, val.raw());
            if constexpr (std::is_same_v<T, smol::math::vec4_t>) glUniform4fv(location, 1, val.raw());
            if constexpr (std::is_same_v<T, smol::color_t>) glUniform4fv(location, 1, val.data);
            if constexpr (std::is_same_v<T, smol::math::mat4_t>) glUniformMatrix4fv(location, 1, GL_FALSE, val.raw()); },
                   value);
    }

    GLuint shader_asset_t::get_program_id() const
    {
        return program_id;
    }

    // call only in main thread and after bind()
    void shader_asset_t::bind_texture(const std::string& name, const texture_asset_t& texture, GLuint unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, texture.get_texture_id());

        GLint location = glGetUniformLocation(program_id, name.c_str());
        if (location == -1)
        {
            SMOL_LOG_ERROR("SHADER", "Could not set sampler with name '{}'", name);
            return;
        }
        glUniform1f(location, unit);
    }
} // namespace smol::asset