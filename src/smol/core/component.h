#pragma once

#include "smol/defines.h"

#include <memory>

namespace smol::core
{
    class gameobject_t;

    class SMOL_API component_t
    {
      public:
        component_t() : active(true) {}
        component_t(bool state) : active(state) {}
        virtual ~component_t() = default;

        virtual void start() {};
        virtual void update([[maybe_unused]] f64 delta_time) {};
        virtual void fixed_update([[maybe_unused]] f64 fixed_timestep) {};

        const std::shared_ptr<gameobject_t> get_gameobject() const { return owner.lock(); }
        void set_owner(std::shared_ptr<gameobject_t> new_owner);

        bool is_active() const { return active; }
        void set_active(bool state) { active = state; }

      private:
        std::weak_ptr<gameobject_t> owner;
        bool active;
    };
} // namespace smol::core