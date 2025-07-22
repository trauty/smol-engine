#pragma once

#include "core/component.h"

#include <string>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

namespace smol::physics
{
    using layer_id_t = u8_t;   
}

namespace smol::components
{
    class collider_ct : public smol::core::component_t
    {
    public:
        virtual ~collider_ct() = default;
        virtual void start();

        virtual JPH::Shape* get_shape() const;

        void set_layer(const std::string& name);
        smol::physics::layer_id_t get_layer_id() const;

    protected:
        JPH::Ref<JPH::Shape> shape;
        JPH::BodyID static_body_id = JPH::BodyID();
        smol::physics::layer_id_t layer_id = 0;
    };
}
