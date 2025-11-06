#pragma once

#include "defines.h"

#include <cglm/cglm.h>

namespace smol::math
{
    struct vec3_t
    {
        union
        {
            struct
            {
                float x, y, z;
            };
            vec3 data;
        };

        constexpr vec3_t() : x(0.0f), y(0.0f), z(0.0f) {}
        constexpr vec3_t(vec3 new_data) { glm_vec3_copy(new_data, data); }
        constexpr vec3_t(f32 x, f32 y, f32 z) : x(x), y(y), z(z) {}

        operator const vec3&() const { return data; }
        vec3& raw() { return data; }
        const vec3& raw() const { return data; }
    };

    struct vec4_t
    {
        union
        {
            struct
            {
                float x, y, z, w;
            };
            vec4 data;
        };

        constexpr vec4_t() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
        constexpr vec4_t(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}

        operator const vec4&() const { return data; }
        vec4& raw() { return data; }
        const vec4& raw() const { return data; }
    };

    struct quat_t
    {
        union
        {
            struct
            {
                float x, y, z, w;
            };
            versor data;
        };

        constexpr quat_t() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
        constexpr quat_t(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}

        operator const versor&() const { return data; }
        versor& raw() { return data; }
        const versor& raw() const { return data; }
    };

    struct mat3_t
    {
        union
        {
            struct
            {
                float m00, m01, m02;
                float m10, m11, m12;
                float m20, m21, m22;
            };
            mat3 data;
        };

        constexpr mat3_t() { glm_mat3_identity(data); }

        operator const mat3&() const { return data; }
        mat3& raw() { return data; }
        const mat3& raw() const { return data; }
    };

    struct mat4_t
    {
        union
        {
            struct
            {
                float m00, m01, m02, m03;
                float m10, m11, m12, m13;
                float m20, m21, m22, m23;
                float m30, m31, m32, m33;
            };
            mat4 data;
        };

        constexpr mat4_t() { glm_mat4_identity(data); }

        operator const mat4&() const { return data; }
        float* raw() { return &data[0][0]; }
        const float* raw() const { return &data[0][0]; }
    };
} // namespace smol::math