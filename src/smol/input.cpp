#include "input.h"

#include "smol/hash.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>
#include <algorithm>
#include <atomic>
#include <cstring>

namespace smol::input
{
    struct input_frame_state_t
    {
        bool cur_key[(size_t)key_t::Count];
        bool prev_key[(size_t)key_t::Count];

        bool cur_mouse[(size_t)key_t::Count];
        bool prev_mouse[(size_t)key_t::Count];

        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        float scroll_delta = 0.0f;
    };

    struct action_listener_t
    {
        listener_id_t id;
        input_state_t triggerState;
        input_callback_t callback;
    };

    namespace
    {
        input_frame_state_t input_state;
        std::vector<key_t> scancode_map;
        std::vector<std::vector<action_id_t>> keybinds;
        std::unordered_map<action_id_t, std::vector<action_listener_t>> listeners;

        void build_lookup_table()
        {
            scancode_map.resize(512, key_t::Unknown);

            scancode_map[SDL_SCANCODE_A] = key_t::A;
            scancode_map[SDL_SCANCODE_B] = key_t::B;
            scancode_map[SDL_SCANCODE_C] = key_t::C;
            scancode_map[SDL_SCANCODE_D] = key_t::D;
            scancode_map[SDL_SCANCODE_E] = key_t::E;
            scancode_map[SDL_SCANCODE_F] = key_t::F;
            scancode_map[SDL_SCANCODE_G] = key_t::G;
            scancode_map[SDL_SCANCODE_H] = key_t::H;
            scancode_map[SDL_SCANCODE_I] = key_t::I;
            scancode_map[SDL_SCANCODE_J] = key_t::J;
            scancode_map[SDL_SCANCODE_K] = key_t::K;
            scancode_map[SDL_SCANCODE_L] = key_t::L;
            scancode_map[SDL_SCANCODE_M] = key_t::M;
            scancode_map[SDL_SCANCODE_N] = key_t::N;
            scancode_map[SDL_SCANCODE_O] = key_t::O;
            scancode_map[SDL_SCANCODE_P] = key_t::P;
            scancode_map[SDL_SCANCODE_Q] = key_t::Q;
            scancode_map[SDL_SCANCODE_R] = key_t::R;
            scancode_map[SDL_SCANCODE_S] = key_t::S;
            scancode_map[SDL_SCANCODE_T] = key_t::T;
            scancode_map[SDL_SCANCODE_U] = key_t::U;
            scancode_map[SDL_SCANCODE_V] = key_t::V;
            scancode_map[SDL_SCANCODE_W] = key_t::W;
            scancode_map[SDL_SCANCODE_X] = key_t::X;
            scancode_map[SDL_SCANCODE_Y] = key_t::Y;
            scancode_map[SDL_SCANCODE_Z] = key_t::Z;

            scancode_map[SDL_SCANCODE_0] = key_t::Num0;
            scancode_map[SDL_SCANCODE_1] = key_t::Num1;
            scancode_map[SDL_SCANCODE_2] = key_t::Num2;
            scancode_map[SDL_SCANCODE_3] = key_t::Num3;
            scancode_map[SDL_SCANCODE_4] = key_t::Num4;
            scancode_map[SDL_SCANCODE_5] = key_t::Num5;
            scancode_map[SDL_SCANCODE_6] = key_t::Num6;
            scancode_map[SDL_SCANCODE_7] = key_t::Num7;
            scancode_map[SDL_SCANCODE_8] = key_t::Num8;
            scancode_map[SDL_SCANCODE_9] = key_t::Num9;

            scancode_map[SDL_SCANCODE_ESCAPE] = key_t::Escape;
            scancode_map[SDL_SCANCODE_RETURN] = key_t::Enter;
            scancode_map[SDL_SCANCODE_TAB] = key_t::Tab;
            scancode_map[SDL_SCANCODE_BACKSPACE] = key_t::Backspace;
            scancode_map[SDL_SCANCODE_SPACE] = key_t::Space;
            scancode_map[SDL_SCANCODE_LSHIFT] = key_t::LeftShift;
            scancode_map[SDL_SCANCODE_RSHIFT] = key_t::RightShift;
            scancode_map[SDL_SCANCODE_LCTRL] = key_t::LeftCtrl;
            scancode_map[SDL_SCANCODE_RCTRL] = key_t::RightCtrl;
            scancode_map[SDL_SCANCODE_LALT] = key_t::LeftAlt;
            scancode_map[SDL_SCANCODE_RALT] = key_t::RightAlt;

            scancode_map[SDL_SCANCODE_LEFT] = key_t::Left;
            scancode_map[SDL_SCANCODE_RIGHT] = key_t::Right;
            scancode_map[SDL_SCANCODE_UP] = key_t::Up;
            scancode_map[SDL_SCANCODE_DOWN] = key_t::Down;

            scancode_map[SDL_SCANCODE_F1] = key_t::F1;
            scancode_map[SDL_SCANCODE_F2] = key_t::F2;
            scancode_map[SDL_SCANCODE_F3] = key_t::F3;
            scancode_map[SDL_SCANCODE_F4] = key_t::F4;
            scancode_map[SDL_SCANCODE_F5] = key_t::F5;
            scancode_map[SDL_SCANCODE_F6] = key_t::F6;
            scancode_map[SDL_SCANCODE_F7] = key_t::F7;
            scancode_map[SDL_SCANCODE_F8] = key_t::F8;
            scancode_map[SDL_SCANCODE_F9] = key_t::F9;
            scancode_map[SDL_SCANCODE_F10] = key_t::F10;
            scancode_map[SDL_SCANCODE_F11] = key_t::F11;
            scancode_map[SDL_SCANCODE_F12] = key_t::F12;
        }

        void dispatch_action(key_t key_t, input_state_t state)
        {
            if ((size_t)key_t >= keybinds.size()) { return; }

            const std::vector<action_id_t>& actions = keybinds[(size_t)key_t];
            for (action_id_t id : actions)
            {
                auto iter = listeners.find(id);
                if (iter != listeners.end())
                {
                    input_context_t ctx = {id, state, key_t};
                    for (const action_listener_t& listener : iter->second)
                    {
                        if (listener.triggerState == state) { listener.callback(ctx); }
                    }
                }
            }
        }

        void processHoldEvents()
        {
            for (size_t i = 0; i < (size_t)key_t::Count; i++)
            {
                if (input_state.cur_key[i]) { dispatch_action((key_t)i, input_state_t::HOLDING); }
            }
        }
    } // namespace

    bool get_key(key_t key) { return input_state.cur_key[(i32_t)key]; }

    bool get_key_down(key_t key) { return input_state.cur_key[(i32_t)key] && !input_state.prev_key[(i32_t)key]; }

    bool get_key_up(key_t key) { return !input_state.cur_key[(i32_t)key] && input_state.prev_key[(i32_t)key]; }

    bool get_mouse_button(mouse_button_t button) { return input_state.cur_mouse[(i32_t)button]; }

    bool get_mouse_button_down(mouse_button_t button)
    { return input_state.cur_mouse[(i32_t)button] && !input_state.prev_mouse[(i32_t)button]; }

    bool get_mouse_button_up(mouse_button_t button)
    { return !input_state.cur_mouse[(i32_t)button] && input_state.prev_mouse[(i32_t)button]; }

    void get_mouse_position(float* x, float* y)
    {
        *x = input_state.mouse_x;
        *y = input_state.mouse_y;
    }

    float get_mouse_x() { return input_state.mouse_x; }

    float get_mouse_y() { return input_state.mouse_y; }

    float get_scroll_delta() { return input_state.scroll_delta; }

    void bind_button(const std::string& action_name, key_t key)
    {
        if ((size_t)key >= keybinds.size()) { return; }

        action_id_t id = smol::hash_string(action_name.c_str());

        std::vector<action_id_t>& bindings = keybinds[(size_t)key];
        if (std::find(bindings.begin(), bindings.end(), id) == bindings.end()) { bindings.push_back(id); }
    }

    listener_id_t on_action(const std::string& action_name, input_state_t state, input_callback_t callback)
    {
        action_id_t id = smol::hash_string(action_name.c_str());

        static std::atomic<u32_t> nextId{0};
        listener_id_t listener_id_t = nextId.fetch_add(1, std::memory_order_relaxed);

        listeners[id].push_back({listener_id_t, state, callback});
        return listener_id_t;
    }

    void remove_listener(listener_id_t id)
    {
        for (auto& [action_id, listeners] : listeners)
        {
            auto iter = std::remove_if(listeners.begin(), listeners.end(),
                                       [id](const action_listener_t& listener) { return listener.id == id; });

            if (iter != listeners.end())
            {
                listeners.erase(iter, listeners.end());
                return;
            }
        }
    }

    void unbind_button(const std::string& action_name, key_t key)
    {
        if ((size_t)key >= keybinds.size()) { return; }

        action_id_t id = smol::hash_string(action_name.c_str());
        std::vector<action_id_t>& bindings = keybinds[(size_t)key];

        bindings.erase(std::remove(bindings.begin(), bindings.end(), id), bindings.end());
    }

    void unbind_all_buttons(const std::string& action_name)
    {
        action_id_t id = smol::hash_string(action_name.c_str());

        for (std::vector<action_id_t>& bindings : keybinds)
        {
            bindings.erase(std::remove(bindings.begin(), bindings.end(), id), bindings.end());
        }
    }

    namespace detail
    {
        void init()
        {
            std::memset(&input_state, 0, sizeof(input_state_t));
            build_lookup_table();
            keybinds.resize((size_t)key_t::Count);
        }

        void prepare_update()
        {
            std::memcpy(input_state.prev_key, input_state.cur_key, sizeof(input_state.cur_key));
            std::memcpy(input_state.prev_mouse, input_state.cur_mouse, sizeof(input_state.cur_mouse));
            input_state.scroll_delta = 0.0f;

            processHoldEvents();
        }

        void process(const SDL_Event& event)
        {
            switch (event.type)
            {
            case SDL_EVENT_KEY_DOWN:
                if (event.key.repeat == 0 && event.key.scancode < scancode_map.size())
                {
                    key_t key_t = scancode_map[event.key.scancode];
                    if (key_t != key_t::Unknown)
                    {
                        input_state.cur_key[(i32_t)key_t] = true;
                        dispatch_action(key_t, input_state_t::PRESSED);
                    }
                }
                break;

            case SDL_EVENT_KEY_UP:
                if (event.key.scancode < scancode_map.size())
                {
                    key_t key = scancode_map[event.key.scancode];
                    if (key != key_t::Unknown)
                    {
                        input_state.cur_key[(i32_t)key] = false;
                        dispatch_action(key, input_state_t::RELEASED);
                    }
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button > 0 && event.button.button <= 3)
                {
                    input_state.cur_mouse[event.button.button - 1] = true;
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button > 0 && event.button.button <= 3)
                {
                    input_state.cur_mouse[event.button.button - 1] = false;
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                input_state.mouse_x = event.motion.x;
                input_state.mouse_y = event.motion.y;
                break;

            case SDL_EVENT_MOUSE_WHEEL: input_state.scroll_delta = event.wheel.y; break;
            }
        }
    } // namespace detail
} // namespace smol::input