#pragma once

#include "smol/color.h"
#include "smol/defines.h"
#include "smol/math.h"

namespace smol::tween
{
    constexpr u32_t INFINITE_LOOPS = 0xFFFFFFFFu;

    enum class ease_e : u8_t
    {
        LINEAR,
        QUAD_IN,
        QUAD_OUT,
        QUAD_IN_OUT,
        CUBIC_IN,
        CUBIC_OUT,
        CUBIC_IN_OUT,
        QUART_IN,
        QUART_OUT,
        QUART_IN_OUT,
        SINE_IN,
        SINE_OUT,
        SINE_IN_OUT,
        EXPO_IN,
        EXPO_OUT,
        EXPO_IN_OUT,
        CIRC_IN,
        CIRC_OUT,
        CIRC_IN_OUT,
        BACK_IN,
        BACK_OUT,
        BACK_IN_OUT,
        ELASTIC_IN,
        ELASTIC_OUT,
        ELASTIC_IN_OUT,
        BOUNCE_IN,
        BOUNCE_OUT,
        BOUNCE_IN_OUT,
    };

    SMOL_ENGINE_API f32 evaluate(ease_e ease, f32 t);

    struct handle_t
    {
        u32_t index = 0;
        u32_t generation = 0;

        bool operator==(const handle_t& other) const { return index == other.index && generation == other.generation; }
    };

    class builder_t
    {
      public:
        explicit builder_t(handle_t handle) : m_handle(handle) {}

        SMOL_ENGINE_API builder_t& delay(f32 seconds);

        SMOL_ENGINE_API builder_t& loops(u32_t count);

        SMOL_ENGINE_API builder_t& ping_pong(bool enabled = true);

        SMOL_ENGINE_API builder_t& speed(f32 scale);

        SMOL_ENGINE_API builder_t& from(f32 value);
        SMOL_ENGINE_API builder_t& from(vec2_t value);
        SMOL_ENGINE_API builder_t& from(vec3_t value);
        SMOL_ENGINE_API builder_t& from(color_t value);

        operator handle_t() const { return m_handle; }
        handle_t handle() const { return m_handle; }

      private:
        handle_t m_handle;
    };

    SMOL_ENGINE_API builder_t to(f32* target, f32 destination, f32 duration, ease_e ease = ease_e::LINEAR);
    SMOL_ENGINE_API builder_t to(vec2_t* target, vec2_t destination, f32 duration, ease_e ease = ease_e::LINEAR);
    SMOL_ENGINE_API builder_t to(vec3_t* target, vec3_t destination, f32 duration, ease_e ease = ease_e::LINEAR);
    SMOL_ENGINE_API builder_t to(color_t* target, color_t destination, f32 duration, ease_e ease = ease_e::LINEAR);

    SMOL_ENGINE_API bool is_active(handle_t handle);

    SMOL_ENGINE_API void cancel(handle_t handle);

    SMOL_ENGINE_API void complete(handle_t handle);

    SMOL_ENGINE_API void pause(handle_t handle, bool paused);

    SMOL_ENGINE_API void cancel_target(const void* address, u64_t size);

    SMOL_ENGINE_API void cancel_all();

    SMOL_ENGINE_API u32_t active_count();

    SMOL_ENGINE_API void update(f32 dt);
} // namespace smol::tween
