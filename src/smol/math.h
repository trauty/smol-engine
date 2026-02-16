#pragma once

#include "cglm/euler.h"
#include "defines.h"

#include <cglm/cglm.h>
#include <cstring>

namespace smol
{
    struct vec3_t
    {
        float x, y, z;

        vec3_t() : x(0.0f), y(0.0f), z(0.0f) {}
        vec3_t(f32 x, f32 y, f32 z) : x(x), y(y), z(z) {}
        vec3_t(float* v) : x(v[0]), y(v[1]), z(v[2]) {}

        operator float*() { return &x; }
        operator const float*() const { return &x; }
    };

    struct alignas(16) vec4_t
    {
        float x, y, z, w;

        vec4_t() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
        vec4_t(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}
        vec4_t(float* v) : x(v[0]), y(v[1]), z(v[2]), w(v[3]) {}

        operator float*() { return &x; }
        operator const float*() const { return &x; }
    };

    struct alignas(16) quat_t
    {
        float x, y, z, w;

        quat_t() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
        quat_t(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}
        quat_t(float* v) : x(v[0]), y(v[1]), z(v[2]), w(v[3]) {}

        static quat_t from_euler(vec3_t euler_angles)
        {
            quat_t dest;
            glm_euler_zyx_quat(euler_angles, dest);
            return dest;
        }

        operator float*() { return &x; }
        operator const float*() const { return &x; }
    };

    struct mat3_t
    {
        float m00, m01, m02;
        float m10, m11, m12;
        float m20, m21, m22;

        mat3_t()
        {
            std::memset(this, 0, sizeof(mat3_t));
            m00 = 1.0f;
            m11 = 1.0f;
            m22 = 1.0f;
        }

        operator float*() { return &m00; }
        operator const float*() const { return &m00; }

        operator vec3*() { return (vec3*)&m00; }
        operator const vec3*() const { return (const vec3*)&m00; }

        float* operator[](int col) { return &m00 + (col * 3); }
        const float* operator[](int col) const { return &m00 + (col * 3); }
    };

    struct alignas(32) mat4_t
    {
        float m00, m01, m02, m03;
        float m10, m11, m12, m13;
        float m20, m21, m22, m23;
        float m30, m31, m32, m33;

        mat4_t()
        {
            std::memset(this, 0, sizeof(mat4_t));
            m00 = 1.0f;
            m11 = 1.0f;
            m22 = 1.0f;
            m33 = 1.0f;
        }

        operator float*() { return &m00; }
        operator const float*() const { return &m00; }

        operator vec4*() { return (vec4*)&m00; }
        operator const vec4*() const { return (const vec4*)&m00; }

        float* operator[](int col) { return &m00 + (col * 4); }
        const float* operator[](int col) const { return &m00 + (col * 4); }
    };
} // namespace smol