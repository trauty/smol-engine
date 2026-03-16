#include "physics_world.h"

#include "Jolt/Core/Factory.h"
#include "Jolt/Core/IssueReporting.h"
#include "Jolt/Core/Memory.h"
#include "Jolt/Core/Reference.h"
#include "Jolt/Core/TempAllocator.h"
#include "Jolt/Math/Quat.h"
#include "Jolt/Math/Vec3.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Body/BodyID.h"
#include "Jolt/Physics/Body/BodyInterface.h"
#include "Jolt/Physics/Body/MotionType.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/Shape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/EActivation.h"
#include "Jolt/RegisterTypes.h"
#include "smol/components/physics.h"
#include "smol/components/transform.h"
#include "smol/ecs.h"
#include "smol/ecs_fwd.h"
#include "smol/log.h"
#include "smol/physics/jolt_job_system_int.h"
#include "smol/time.h"

#include <cstdarg>
#include <cstdio>
#include <vector>

namespace smol
{
    class bp_layer_interface_impl_t final : public JPH::BroadPhaseLayerInterface
    {
      public:
        virtual JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
        virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
        { return (inLayer == physics::layers::NON_MOVING) ? JPH::BroadPhaseLayer(0) : JPH::BroadPhaseLayer(1); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
        { return (inLayer.GetValue() == 0) ? "NON_MOVING" : "MOVING"; }
#endif
    };

    class object_vs_broad_phase_layer_filter_impl_t : public JPH::ObjectVsBroadPhaseLayerFilter
    {
      public:
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
        {
            if (inLayer1 == physics::layers::NON_MOVING) return inLayer2.GetValue() == 1;
            return true;
        }
    };

    class object_layer_pair_filter_impl_t : public JPH::ObjectLayerPairFilter
    {
      public:
        virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
        { return !(inObject1 == physics::layers::NON_MOVING && inObject2 == physics::layers::NON_MOVING); }
    };

    void on_rigidbody_destroyed(ecs::registry_t& reg, ecs::entity_t entity)
    {
        rigidbody_t& rb = reg.get<rigidbody_t>(entity);
        if (rb.is_initiaklized && !rb.body_id.IsInvalid())
        {
            physics_world_t* physics = reg.ctx().get<physics_world_t*>();
            JPH::BodyInterface& body_interface = physics->system.GetBodyInterface();
            body_interface.RemoveBody(rb.body_id);
            body_interface.DestroyBody(rb.body_id);
            rb.is_initiaklized = false;
        }
    }

    static void jph_trace_impl(const char* in_fmt, ...)
    {
        va_list list;
        va_start(list, in_fmt);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), in_fmt, list);
        va_end(list);
        SMOL_LOG_FATAL("JPH", "{}", buffer);
    }

    void physics_world_t::init(ecs::registry_t& reg)
    {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = jph_trace_impl;
        JPH::Factory::sInstance = new JPH::Factory;
        JPH::RegisterTypes();

        temp_allocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
        job_integration = new jolt_job_system_integration_t();

        bp_interface = new bp_layer_interface_impl_t();
        object_vs_bp_filter = new object_vs_broad_phase_layer_filter_impl_t();
        object_vs_object_filter = new object_layer_pair_filter_impl_t();

        system.Init(1024, 0, 1024, 1024, *bp_interface, *object_vs_bp_filter, *object_vs_object_filter);
        reg.on_destroy<rigidbody_t>().connect<&on_rigidbody_destroyed>();
    }

    void physics_world_t::update() { system.Update(time::fixed_dt, 1, temp_allocator, job_integration); }

    void physics_world_t::shutdown()
    {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        delete job_integration;
        delete temp_allocator;
        delete bp_interface;
        delete object_vs_bp_filter;
        delete object_vs_object_filter;
    }

    void physics_world_t::create_bodies(ecs::registry_t& reg)
    {
        JPH::BodyInterface& body_interface = system.GetBodyInterface();

        for (auto [entity, rb, transform] : reg.view<rigidbody_t, transform_t>().each())
        {
            if (rb.is_initiaklized) { continue; }

            JPH::RefConst<JPH::Shape> shape;

            if (box_collider_t* col = reg.try_get<box_collider_t>(entity))
            {
                shape = new JPH::BoxShape(JPH::Vec3(col->extents.x, col->extents.y, col->extents.z));
            }
            else if (sphere_collider_t* col = reg.try_get<sphere_collider_t>(entity))
            {
                shape = new JPH::SphereShape(col->radius);
            }
            else
            {
                shape = new JPH::BoxShape(JPH::Vec3(0.5, 0.5, 0.5f));
            }

            JPH::BodyCreationSettings settings(
                shape, JPH::Vec3(transform.world_mat[3][0], transform.world_mat[3][1], transform.world_mat[3][2]),
                JPH::Quat(transform.local_rotation.x, transform.local_rotation.y, transform.local_rotation.z,
                          transform.local_rotation.w),
                rb.type == body_type_e::STATIC ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic,
                rb.type == body_type_e::STATIC ? physics::layers::NON_MOVING : physics::layers::MOVING);

            rb.body_id = body_interface.CreateAndAddBody(settings, JPH::EActivation::Activate);
            rb.is_initiaklized = true;
        }
    }

    void physics_world_t::destroy_bodies(ecs::registry_t& reg)
    {
        JPH::BodyInterface& body_interface = system.GetBodyInterface();

        std::vector<JPH::BodyID> batch;
        batch.reserve(1024);

        for (auto [entity, rb] : reg.view<rigidbody_t>().each())
        {
            if (rb.is_initiaklized && !rb.body_id.IsInvalid())
            {
                batch.push_back(rb.body_id);
                rb.is_initiaklized = false;
                rb.body_id = JPH::BodyID();
            }

            if (batch.size() >= 1024)
            {
                body_interface.RemoveBodies(batch.data(), batch.size());
                body_interface.DestroyBodies(batch.data(), batch.size());
                batch.clear();
            }
        }

        if (!batch.empty())
        {
            body_interface.RemoveBodies(batch.data(), batch.size());
            body_interface.DestroyBodies(batch.data(), batch.size());
        }
    }
} // namespace smol