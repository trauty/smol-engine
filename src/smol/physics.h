#pragma once

#include "defines.h"

#include <string>

namespace JPH
{
    class PhysicsSystem;
    class BodyInterface;
} // namespace JPH

namespace smol::physics
{
    using layer_id_t = u8_t;

    enum class broad_phase_layers_e : u32_t
    {
        STATIC,
        DYNAMIC,
        COUNT
    };

    inline f32 interpolation_alpha = 0.0f;

    void init();

    layer_id_t set_layer(const std::string& name, broad_phase_layers_e broad_phase_type);
    void set_layer_should_collide(const std::string& a, const std::string& b, bool enable);
    u32_t get_num_layers();
    const std::string& get_layer_name(layer_id_t id);
    layer_id_t get_layer_id(const std::string& name);

    JPH::PhysicsSystem& get_physics_system();
    JPH::BodyInterface& get_body_interface();

    void update(f64 fixed_timestep);

    void shutdown();
} // namespace smol::physics
