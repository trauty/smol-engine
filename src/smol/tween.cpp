#include "tween.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace smol::tween
{
    namespace
    {
        constexpr f32 PI = 3.14159265358979323846f;

        constexpr f32 BACK_C1 = 1.70158f;
        constexpr f32 BACK_C2 = BACK_C1 * 1.525f;
        constexpr f32 BACK_C3 = BACK_C1 + 1.0f;
        constexpr f32 ELASTIC_C4 = (2.0f * PI) / 3.0f;
        constexpr f32 ELASTIC_C5 = (2.0f * PI) / 4.5f;

        f32 bounce_out(f32 t)
        {
            constexpr f32 N = 7.5625f;
            constexpr f32 D = 2.75f;

            if (t < 1.0f / D) { return N * t * t; }
            if (t < 2.0f / D)
            {
                t -= 1.5f / D;
                return N * t * t + 0.75f;
            }
            if (t < 2.5f / D)
            {
                t -= 2.25f / D;
                return N * t * t + 0.9375f;
            }
            t -= 2.625f / D;
            return N * t * t + 0.984375f;
        }

        enum class value_kind_e : u8_t
        {
            F32,
            VEC2,
            VEC3,
            COLOR,
        };

        constexpr u32_t MAX_COMPONENTS = 4;

        u32_t component_count(value_kind_e kind)
        {
            switch (kind)
            {
            case value_kind_e::F32: return 1;
            case value_kind_e::VEC2: return 2;
            case value_kind_e::VEC3: return 3;
            case value_kind_e::COLOR: return 4;
            }
            return 1;
        }

        struct tween_t
        {
            f32* target = nullptr;
            value_kind_e kind = value_kind_e::F32;
            ease_e ease = ease_e::LINEAR;

            f32 from[MAX_COMPONENTS] = {};
            f32 to[MAX_COMPONENTS] = {};

            f32 duration = 0.0f;
            f32 elapsed = 0.0f;
            f32 delay = 0.0f;
            f32 speed = 1.0f;

            u32_t loops_total = 1;
            u32_t loops_done = 0;
            bool ping_pong = false;
            bool paused = false;

            bool capture_from = true;
            bool started = false;

            u32_t generation = 0;
            bool active = false;
        };

        std::vector<tween_t> g_tweens;
        std::vector<u32_t> g_free_slots;
        u32_t g_next_generation = 1;

        tween_t* resolve(handle_t handle)
        {
            if (handle.generation == 0 || handle.index >= g_tweens.size()) { return nullptr; }

            tween_t& tween = g_tweens[handle.index];
            if (!tween.active || tween.generation != handle.generation) { return nullptr; }

            return &tween;
        }

        void release(tween_t& tween)
        {
            tween.active = false;
            tween.target = nullptr;
            g_free_slots.push_back(static_cast<u32_t>(&tween - g_tweens.data()));
        }

        void write(const tween_t& tween, f32 progress)
        {
            const u32_t count = component_count(tween.kind);
            for (u32_t i = 0; i < count; i++)
            {
                tween.target[i] = tween.from[i] + (tween.to[i] - tween.from[i]) * progress;
            }
        }

        void capture(tween_t& tween)
        {
            if (!tween.capture_from) { return; }

            const u32_t count = component_count(tween.kind);
            for (u32_t i = 0; i < count; i++) { tween.from[i] = tween.target[i]; }
        }

        handle_t create(f32* target, value_kind_e kind, const f32* destination, f32 duration, ease_e ease)
        {
            if (target == nullptr) { return handle_t{}; }

            u32_t index;
            if (!g_free_slots.empty())
            {
                index = g_free_slots.back();
                g_free_slots.pop_back();
            }
            else
            {
                index = static_cast<u32_t>(g_tweens.size());
                g_tweens.emplace_back();
            }

            tween_t& tween = g_tweens[index];
            tween = tween_t{};
            tween.target = target;
            tween.kind = kind;
            tween.ease = ease;
            tween.duration = duration;
            tween.generation = g_next_generation++;
            if (g_next_generation == 0) { g_next_generation = 1; }
            tween.active = true;

            const u32_t count = component_count(kind);
            for (u32_t i = 0; i < count; i++) { tween.to[i] = destination[i]; }

            if (duration <= 0.0f)
            {
                capture(tween);
                write(tween, 1.0f);
                const handle_t dead{index, tween.generation};
                release(tween);
                return dead;
            }

            return handle_t{index, tween.generation};
        }

        bool step(tween_t& tween, f32 dt)
        {
            if (tween.paused) { return true; }

            f32 scaled = dt * tween.speed;

            if (tween.delay > 0.0f)
            {
                tween.delay -= scaled;
                if (tween.delay > 0.0f) { return true; }

                scaled = -tween.delay;
                tween.delay = 0.0f;
            }

            if (!tween.started)
            {
                capture(tween);
                tween.started = true;
            }

            tween.elapsed += scaled;

            while (tween.elapsed >= tween.duration)
            {
                const bool infinite = tween.loops_total == INFINITE_LOOPS;
                if (!infinite && tween.loops_done + 1 >= tween.loops_total)
                {
                    write(tween, tween.ping_pong && (tween.loops_done % 2 == 1) ? 0.0f : 1.0f);
                    return false;
                }

                tween.loops_done++;
                tween.elapsed -= tween.duration;
            }

            f32 t = tween.elapsed / tween.duration;
            if (tween.ping_pong && (tween.loops_done % 2 == 1)) { t = 1.0f - t; }

            write(tween, evaluate(tween.ease, t));
            return true;
        }
    } // namespace

    f32 evaluate(ease_e ease, f32 t)
    {
        t = std::clamp(t, 0.0f, 1.0f);

        switch (ease)
        {
        case ease_e::LINEAR: return t;

        case ease_e::QUAD_IN: return t * t;
        case ease_e::QUAD_OUT: return 1.0f - (1.0f - t) * (1.0f - t);
        case ease_e::QUAD_IN_OUT: return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;

        case ease_e::CUBIC_IN: return t * t * t;
        case ease_e::CUBIC_OUT: return 1.0f - std::pow(1.0f - t, 3.0f);
        case ease_e::CUBIC_IN_OUT: return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;

        case ease_e::QUART_IN: return t * t * t * t;
        case ease_e::QUART_OUT: return 1.0f - std::pow(1.0f - t, 4.0f);
        case ease_e::QUART_IN_OUT:
            return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) / 2.0f;

        case ease_e::SINE_IN: return 1.0f - std::cos((t * PI) / 2.0f);
        case ease_e::SINE_OUT: return std::sin((t * PI) / 2.0f);
        case ease_e::SINE_IN_OUT: return -(std::cos(PI * t) - 1.0f) / 2.0f;

        case ease_e::EXPO_IN: return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
        case ease_e::EXPO_OUT: return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
        case ease_e::EXPO_IN_OUT:
            if (t <= 0.0f) { return 0.0f; }
            if (t >= 1.0f) { return 1.0f; }
            return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f
                            : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;

        case ease_e::CIRC_IN: return 1.0f - std::sqrt(1.0f - t * t);
        case ease_e::CIRC_OUT: return std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f));
        case ease_e::CIRC_IN_OUT:
            return t < 0.5f ? (1.0f - std::sqrt(1.0f - std::pow(2.0f * t, 2.0f))) / 2.0f
                            : (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;

        case ease_e::BACK_IN: return BACK_C3 * t * t * t - BACK_C1 * t * t;
        case ease_e::BACK_OUT: return 1.0f + BACK_C3 * std::pow(t - 1.0f, 3.0f) + BACK_C1 * std::pow(t - 1.0f, 2.0f);
        case ease_e::BACK_IN_OUT:
            return t < 0.5f
                       ? (std::pow(2.0f * t, 2.0f) * ((BACK_C2 + 1.0f) * 2.0f * t - BACK_C2)) / 2.0f
                       : (std::pow(2.0f * t - 2.0f, 2.0f) * ((BACK_C2 + 1.0f) * (t * 2.0f - 2.0f) + BACK_C2) + 2.0f) /
                             2.0f;

        case ease_e::ELASTIC_IN:
            if (t <= 0.0f) { return 0.0f; }
            if (t >= 1.0f) { return 1.0f; }
            return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * ELASTIC_C4);
        case ease_e::ELASTIC_OUT:
            if (t <= 0.0f) { return 0.0f; }
            if (t >= 1.0f) { return 1.0f; }
            return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * ELASTIC_C4) + 1.0f;
        case ease_e::ELASTIC_IN_OUT:
            if (t <= 0.0f) { return 0.0f; }
            if (t >= 1.0f) { return 1.0f; }
            return t < 0.5f
                       ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * ELASTIC_C5)) / 2.0f
                       : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * ELASTIC_C5)) / 2.0f +
                             1.0f;

        case ease_e::BOUNCE_IN: return 1.0f - bounce_out(1.0f - t);
        case ease_e::BOUNCE_OUT: return bounce_out(t);
        case ease_e::BOUNCE_IN_OUT:
            return t < 0.5f ? (1.0f - bounce_out(1.0f - 2.0f * t)) / 2.0f : (1.0f + bounce_out(2.0f * t - 1.0f)) / 2.0f;
        }

        return t;
    }

    builder_t& builder_t::delay(f32 seconds)
    {
        if (tween_t* tween = resolve(m_handle)) { tween->delay = seconds; }
        return *this;
    }

    builder_t& builder_t::loops(u32_t count)
    {
        if (tween_t* tween = resolve(m_handle)) { tween->loops_total = std::max(1u, count); }
        return *this;
    }

    builder_t& builder_t::ping_pong(bool enabled)
    {
        if (tween_t* tween = resolve(m_handle)) { tween->ping_pong = enabled; }
        return *this;
    }

    builder_t& builder_t::speed(f32 scale)
    {
        if (tween_t* tween = resolve(m_handle)) { tween->speed = scale; }
        return *this;
    }

    builder_t& builder_t::from(f32 value)
    {
        if (tween_t* tween = resolve(m_handle))
        {
            tween->from[0] = value;
            tween->capture_from = false;
        }
        return *this;
    }

    builder_t& builder_t::from(vec2_t value)
    {
        if (tween_t* tween = resolve(m_handle))
        {
            tween->from[0] = value.x;
            tween->from[1] = value.y;
            tween->capture_from = false;
        }
        return *this;
    }

    builder_t& builder_t::from(vec3_t value)
    {
        if (tween_t* tween = resolve(m_handle))
        {
            tween->from[0] = value.x;
            tween->from[1] = value.y;
            tween->from[2] = value.z;
            tween->capture_from = false;
        }
        return *this;
    }

    builder_t& builder_t::from(color_t value)
    {
        if (tween_t* tween = resolve(m_handle))
        {
            tween->from[0] = value.r;
            tween->from[1] = value.g;
            tween->from[2] = value.b;
            tween->from[3] = value.a;
            tween->capture_from = false;
        }
        return *this;
    }

    builder_t to(f32* target, f32 destination, f32 duration, ease_e ease)
    { return builder_t{create(target, value_kind_e::F32, &destination, duration, ease)}; }

    builder_t to(vec2_t* target, vec2_t destination, f32 duration, ease_e ease)
    {
        const f32 dest[2] = {destination.x, destination.y};
        return builder_t{create(target ? &target->x : nullptr, value_kind_e::VEC2, dest, duration, ease)};
    }

    builder_t to(vec3_t* target, vec3_t destination, f32 duration, ease_e ease)
    {
        const f32 dest[3] = {destination.x, destination.y, destination.z};
        return builder_t{create(target ? &target->x : nullptr, value_kind_e::VEC3, dest, duration, ease)};
    }

    builder_t to(color_t* target, color_t destination, f32 duration, ease_e ease)
    {
        const f32 dest[4] = {destination.r, destination.g, destination.b, destination.a};
        return builder_t{create(target ? &target->r : nullptr, value_kind_e::COLOR, dest, duration, ease)};
    }

    bool is_active(handle_t handle) { return resolve(handle) != nullptr; }

    void cancel(handle_t handle)
    {
        if (tween_t* tween = resolve(handle)) { release(*tween); }
    }

    void complete(handle_t handle)
    {
        tween_t* tween = resolve(handle);
        if (tween == nullptr) { return; }

        capture(*tween);

        const bool ends_at_start =
            tween->ping_pong && tween->loops_total != INFINITE_LOOPS && (tween->loops_total % 2 == 0);
        write(*tween, ends_at_start ? 0.0f : 1.0f);

        release(*tween);
    }

    void pause(handle_t handle, bool paused)
    {
        if (tween_t* tween = resolve(handle)) { tween->paused = paused; }
    }

    void cancel_target(const void* address, u64_t size)
    {
        const u8_t* begin = static_cast<const u8_t*>(address);
        const u8_t* end = begin + size;

        for (tween_t& tween : g_tweens)
        {
            if (!tween.active) { continue; }

            const u8_t* target = reinterpret_cast<const u8_t*>(tween.target);
            if (target >= begin && target < end) { release(tween); }
        }
    }

    void cancel_all()
    {
        for (tween_t& tween : g_tweens)
        {
            if (tween.active) { release(tween); }
        }
    }

    u32_t active_count()
    {
        u32_t count = 0;
        for (const tween_t& tween : g_tweens)
        {
            if (tween.active) { count++; }
        }
        return count;
    }

    void update(f32 dt)
    {
        if (dt == 0.0f) { return; }

        for (u32_t i = 0; i < g_tweens.size(); i++)
        {
            if (!g_tweens[i].active) { continue; }
            if (!step(g_tweens[i], dt)) { release(g_tweens[i]); }
        }
    }
} // namespace smol::tween