#pragma once

#include "defines.h"
#include "smol/defines.h"

#include <functional>
#include <string>

union SDL_Event;

namespace smol::input
{
    using action_id_t = u32_t;
    using listener_id_t = u32_t;

    enum class key_t : u32_t
    {
        Unknown = 0,

        A,
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,

        Num0,
        Num1,
        Num2,
        Num3,
        Num4,
        Num5,
        Num6,
        Num7,
        Num8,
        Num9,

        Escape,
        Enter,
        Tab,
        Backspace,
        Space,
        LeftShift,
        RightShift,
        LeftCtrl,
        RightCtrl,
        LeftAlt,
        RightAlt,

        Left,
        Right,
        Up,
        Down,

        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,

        Count
    };

    enum class mouse_button_t : uint8_t
    {
        Left = 0,
        Middle,
        Right,
        Count
    };

    enum class input_state_t : uint8_t
    {
        PRESSED,
        RELEASED,
        HOLDING
    };

    struct SMOL_API input_context_t
    {
        action_id_t actionId;
        input_state_t state;
        key_t key;
    };

    using input_callback_t = std::function<void(const input_context_t&)>;

    SMOL_API bool get_key(key_t key);

    SMOL_API bool get_key_down(key_t key);

    SMOL_API bool get_key_up(key_t key);

    SMOL_API bool get_mouse_button(mouse_button_t button);

    SMOL_API bool get_mouse_button_down(mouse_button_t button);

    SMOL_API bool get_mouse_button_up(mouse_button_t button);

    SMOL_API void get_mouse_position(float* x, float* y);

    SMOL_API float get_mouse_x();

    SMOL_API float get_mouse_y();

    SMOL_API float get_scroll_delta();

    SMOL_API void bind_button(const std::string& action_name, key_t key);

    SMOL_API listener_id_t on_action(const std::string& action_name, input_state_t state, input_callback_t callback);

    SMOL_API void remove_listener(listener_id_t id);

    SMOL_API void unbind_button(const std::string& action_name, key_t key);

    SMOL_API void unbind_all_buttons(const std::string& action_name);

    namespace detail
    {
        void init();

        void prepare_update();

        void process(const SDL_Event& event);
    } // namespace detail
} // namespace smol::input
