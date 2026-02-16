#pragma once

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
// clang-format on

#include "Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h"
#include "Jolt/Physics/Collision/ObjectLayer.h"
#include "jolt_job_system_int.h"
#include "smol/ecs.h"

namespace smol
{
    namespace physics::layers
    {
        inline constexpr JPH::ObjectLayer NON_MOVING = 0;
        inline constexpr JPH::ObjectLayer MOVING = 1;
        inline constexpr JPH::ObjectLayer NUM_LAYERS = 2;
    } // namespace physics::layers

    struct physics_world_t
    {
        SMOL_COMPONENT(physics_world_t)

        JPH::PhysicsSystem system;
        JPH::TempAllocatorImpl* temp_allocator = nullptr;

        jolt_job_system_integration_t* job_integration = nullptr;

        JPH::BroadPhaseLayerInterface* bp_interface = nullptr;
        JPH::ObjectVsBroadPhaseLayerFilter* object_vs_bp_filter = nullptr;
        JPH::ObjectLayerPairFilter* object_vs_object_filter = nullptr;

        void init(ecs::registry_t& reg);
        void update();
        void shutdown();

        void create_bodies(ecs::registry_t& reg);
        void destroy_bodies(ecs::registry_t& reg);
    };
} // namespace smol