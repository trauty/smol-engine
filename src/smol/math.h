#pragma once

#include "cglm/euler.h"
#include "cglm/util.h"
#include "cglm/vec3.h"
#include "defines.h"

#include <cglm/cglm.h>
#include <cstring>

namespace smol
{
    namespace math
    {
        inline f32 deg_to_rad(f32 deg) { return glm_rad(deg); }
        inline f32 rad_to_deg(f32 rad) { return glm_deg(rad); }
    } // namespace math

    struct SMOL_API vec2_t
    {
        float x, y;

        vec2_t() : x(0.0f), y(0.0f) {}
        vec2_t(f32 x, f32 y) : x(x), y(y) {}
        vec2_t(const float* v) : x(v[0]), y(v[1]) {}

        operator float*() { return &x; }
        operator float*() const { return const_cast<float*>(&x); }

        static vec2_t normalize(vec2_t v)
        {
            glm_vec2_normalize(v);
            return v;
        }

        static float dot(vec2_t a, vec2_t b) { return glm_vec2_dot(a, b); }

        static float cross(vec2_t a, vec2_t b) { return glm_vec2_cross(a, b); }

        float length() const { return glm_vec2_norm(const_cast<float*>(&x)); }
        float length_squared() const { return glm_vec2_norm2(const_cast<float*>(&x)); }

        vec2_t operator+(vec2_t other) const { return {x + other.x, y + other.y}; }
        vec2_t operator-(vec2_t other) const { return {x - other.x, y - other.y}; }
        vec2_t operator*(vec2_t other) const { return {x * other.x, y * other.y}; }
        vec2_t operator*(float scalar) const { return {x * scalar, y * scalar}; }
        vec2_t operator/(float scalar) const { return {x / scalar, y / scalar}; }

        vec2_t& operator+=(vec2_t other)
        {
            x += other.x;
            y += other.y;
            return *this;
        }
        vec2_t& operator-=(vec2_t other)
        {
            x -= other.x;
            y -= other.y;
            return *this;
        }
        vec2_t& operator*=(float scalar)
        {
            x *= scalar;
            y *= scalar;
            return *this;
        }
    };

    inline vec2_t operator*(float scalar, vec2_t v) { return v * scalar; }

    struct SMOL_API vec3_t
    {
        float x, y, z;

        vec3_t() : x(0.0f), y(0.0f), z(0.0f) {}
        vec3_t(f32 x, f32 y, f32 z) : x(x), y(y), z(z) {}
        vec3_t(const float* v) : x(v[0]), y(v[1]), z(v[2]) {}

        operator float*() { return &x; }
        operator float*() const { return const_cast<float*>(&x); }

        static vec3_t normalize(vec3_t v)
        {
            glm_vec3_normalize(v);
            return v;
        }

        static float dot(vec3_t a, vec3_t b) { return glm_vec3_dot(a, b); }
        static vec3_t cross(vec3_t a, vec3_t b)
        {
            vec3_t res;
            glm_vec3_cross(a, b, res);
            return res;
        }

        float length() const { return glm_vec3_norm(const_cast<float*>(&x)); }
        float length_squared() const { return glm_vec3_norm2(const_cast<float*>(&x)); }

        static vec3_t right() { return {1.0f, 0.0f, 0.0f}; }
        static vec3_t left() { return {-1.0f, 0.0f, 0.0f}; }
        static vec3_t up() { return {0.0f, 1.0f, 0.0f}; }
        static vec3_t down() { return {0.0f, -1.0f, 0.0f}; }
        static vec3_t forward() { return {0.0f, 0.0f, 1.0f}; }
        static vec3_t back() { return {0.0f, 0.0f, -1.0f}; }

        vec3_t operator+(vec3_t other) const { return {x + other.x, y + other.y, z + other.z}; }
        vec3_t operator-(vec3_t other) const { return {x - other.x, y - other.y, z - other.z}; }
        vec3_t operator*(vec3_t other) const { return {x * other.x, y * other.y, z * other.z}; }
        vec3_t operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
        vec3_t operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar}; }

        vec3_t& operator+=(vec3_t other)
        {
            x += other.x;
            y += other.y;
            z += other.z;
            return *this;
        }
        vec3_t& operator-=(vec3_t other)
        {
            x -= other.x;
            y -= other.y;
            z -= other.z;
            return *this;
        }
        vec3_t& operator*=(float scalar)
        {
            x *= scalar;
            y *= scalar;
            z *= scalar;
            return *this;
        }
    };

    inline vec3_t operator*(float scalar, vec3_t v) { return v * scalar; }

    struct alignas(16) SMOL_API vec4_t
    {
        float x, y, z, w;

        vec4_t() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
        vec4_t(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}
        vec4_t(const float* v) : x(v[0]), y(v[1]), z(v[2]), w(v[3]) {}

        operator float*() { return &x; }
        operator float*() const { return const_cast<float*>(&x); }

        vec4_t operator+(vec4_t other) const { return {x + other.x, y + other.y, z + other.z, w + other.w}; }
        vec4_t operator-(vec4_t other) const { return {x - other.x, y - other.y, z - other.z, w - other.w}; }
        vec4_t operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar, w * scalar}; }
    };

    struct alignas(16) SMOL_API quat_t
    {
        float x, y, z, w;

        quat_t() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
        quat_t(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}
        quat_t(const float* v) : x(v[0]), y(v[1]), z(v[2]), w(v[3]) {}

        static quat_t from_euler(vec3_t euler_angles)
        {
            quat_t dest;
            glm_euler_zyx_quat(euler_angles, dest);
            return dest;
        }

        static quat_t normalize(quat_t q)
        {
            glm_quat_normalize(q);
            return q;
        }

        static quat_t slerp(quat_t from, quat_t to, float t)
        {
            quat_t res;
            glm_quat_slerp(from, to, t, res);
            return res;
        }

        static quat_t angle_axis(f32 angle_radians, vec3_t axis)
        {
            quat_t dest;
            glm_quatv(dest, angle_radians, axis);
            return dest;
        }

        vec3_t right() const
        {
            vec3_t res;
            vec3_t global_right = {1.0f, 0.0f, 0.0f};
            glm_quat_rotatev(const_cast<float*>(&x), global_right, res);
            return res;
        }

        vec3_t up() const
        {
            vec3_t res;
            vec3_t global_up = {0.0f, 1.0f, 0.0f};
            glm_quat_rotatev(const_cast<float*>(&x), global_up, res);
            return res;
        }

        vec3_t forward() const
        {
            vec3_t res;
            vec3_t global_fwd = {0.0f, 0.0f, 1.0f};
            glm_quat_rotatev(const_cast<float*>(&x), global_fwd, res);
            return res;
        }

        quat_t operator*(quat_t other) const
        {
            quat_t res;
            glm_quat_mul(const_cast<float*>(&x), other, res);
            return res;
        }

        operator float*() { return &x; }
        operator float*() const { return const_cast<float*>(&x); }
    };

    struct SMOL_API mat3_t
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
        operator float*() const { return const_cast<float*>(&m00); }
        operator vec3*() { return (vec3*)&m00; }
        operator vec3*() const { return (vec3*)const_cast<float*>(&m00); }
        float* operator[](int col) { return &m00 + (col * 3); }
        const float* operator[](int col) const { return const_cast<float*>(&m00 + (col * 3)); }
    };

    struct alignas(32) SMOL_API mat4_t
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

        static mat4_t identity() { return mat4_t(); }

        static mat4_t inverse(const mat4_t& m)
        {
            mat4_t res;
            glm_mat4_inv(m, res);
            return res;
        }

        static mat4_t transpose(const mat4_t& m)
        {
            mat4_t res;
            glm_mat4_transpose_to(m, res);
            return res;
        }

        static mat4_t translate(const mat4_t& m, vec3_t v)
        {
            mat4_t res = m;
            glm_translate(res, v);
            return res;
        }

        static mat4_t scale(const mat4_t& m, vec3_t v)
        {
            mat4_t res = m;
            glm_scale(res, v);
            return res;
        }

        static mat4_t rotate(const mat4_t& m, quat_t q)
        {
            mat4_t res;
            mat4_t rot_mat;
            glm_quat_mat4(q, rot_mat);
            glm_mat4_mul(m, rot_mat, res);
            return res;
        }

        static mat4_t look_at(vec3_t eye, vec3_t center, vec3_t up)
        {
            mat4_t res;
            glm_lookat(eye, center, up, res);
            return res;
        }

        static mat4_t perspective(float fovy, float aspect, float nearVal, float farVal)
        {
            mat4_t res;
            glm_perspective(fovy, aspect, nearVal, farVal, res);
            return res;
        }

        vec3_t right() const { return {m00, m01, m02}; }
        vec3_t up() const { return {m10, m11, m12}; }
        vec3_t forward() const { return {m20, m21, m22}; }

        mat4_t operator*(const mat4_t& other) const
        {
            mat4_t res;
            glm_mat4_mul(const_cast<vec4*>((const vec4*)this), other, res);
            return res;
        }

        vec4_t operator*(vec4_t v) const
        {
            vec4_t res;
            glm_mat4_mulv(const_cast<vec4*>((const vec4*)this), v, res);
            return res;
        }

        operator float*() { return &m00; }
        operator float*() const { return const_cast<float*>(&m00); }
        operator vec4*() { return (vec4*)&m00; }
        operator vec4*() const { return (vec4*)const_cast<float*>(&m00); }
        float* operator[](int col) { return &m00 + (col * 4); }
        const float* operator[](int col) const { return const_cast<float*>(&m00 + (col * 4)); }
    };
} // namespace smol