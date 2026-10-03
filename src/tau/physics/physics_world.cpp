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
#include "Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h"
#include "Jolt/Physics/Collision/Shape/Shape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/EActivation.h"
#include "Jolt/RegisterTypes.h"
#include "tau/components/physics.h"
#include "tau/components/transform.h"
#include "tau/ecs_fwd.h"
#include "tau/hash.h"
#include "tau/log.h"
#include "tau/physics/jolt_job_system_int.h"
#include "tau/time.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace tau
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

    JPH::EMotionType motion_type_for(body_type_e type)
    {
        switch (type)
        {
        case body_type_e::STATIC: return JPH::EMotionType::Static;
        case body_type_e::KINEMATIC: return JPH::EMotionType::Kinematic;
        case body_type_e::DYNAMIC: break;
        }
        return JPH::EMotionType::Dynamic;
    }

    JPH::ObjectLayer object_layer_for(body_type_e type)
    { return type == body_type_e::STATIC ? physics::layers::NON_MOVING : physics::layers::MOVING; }

    JPH::EActivation activation_for(body_type_e type)
    { return type == body_type_e::STATIC ? JPH::EActivation::DontActivate : JPH::EActivation::Activate; }

    u64_t mix(u64_t seed, f32 value)
    {
        u32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        for (u32_t i = 0; i < sizeof(bits); i++)
        {
            seed ^= static_cast<u8_t>(bits >> (i * 8));
            seed *= 1099511628211ull;
        }
        return seed;
    }

    struct collider_shape_t
    {
        JPH::RefConst<JPH::Shape> shape;
        u64_t id = 0;
    };

    collider_shape_t build_collider_shape(ecs::registry_t& reg, ecs::entity_t entity)
    {
        collider_shape_t out;

        if (box_collider_t* col = reg.try_get<box_collider_t>(entity))
        {
            out.id = hash_string64("box_collider_t");
            out.id = mix(out.id, col->extents.x);
            out.id = mix(out.id, col->extents.y);
            out.id = mix(out.id, col->extents.z);
            out.id = mix(out.id, col->offset.x);
            out.id = mix(out.id, col->offset.y);
            out.id = mix(out.id, col->offset.z);

            out.shape = new JPH::BoxShape(JPH::Vec3(col->extents.x, col->extents.y, col->extents.z));

            if (col->offset.x != 0.0f || col->offset.y != 0.0f || col->offset.z != 0.0f)
            {
                out.shape = new JPH::RotatedTranslatedShape(JPH::Vec3(col->offset.x, col->offset.y, col->offset.z),
                                                            JPH::Quat::sIdentity(), out.shape);
            }

            return out;
        }

        if (sphere_collider_t* col = reg.try_get<sphere_collider_t>(entity))
        {
            out.id = mix(hash_string64("sphere_collider_t"), col->radius);
            out.shape = new JPH::SphereShape(col->radius);
            return out;
        }

        out.id = hash_string64("default_box");
        out.shape = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
        return out;
    }

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
        TAU_LOG_FATAL("JPH", "{}", buffer);
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
            if (rb.is_initiaklized)
            {
                if (rb.applied_type != rb.type)
                {
                    body_interface.SetObjectLayer(rb.body_id, object_layer_for(rb.type));
                    body_interface.SetMotionType(rb.body_id, motion_type_for(rb.type), activation_for(rb.type));

                    if (rb.type != body_type_e::DYNAMIC)
                    {
                        body_interface.SetLinearAndAngularVelocity(rb.body_id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
                    }

                    rb.applied_type = rb.type;
                }

                if (rb.applied_is_sensor != rb.is_sensor)
                {
                    body_interface.SetIsSensor(rb.body_id, rb.is_sensor);
                    rb.applied_is_sensor = rb.is_sensor;
                }

                const collider_shape_t current = build_collider_shape(reg, entity);
                if (rb.applied_shape_id != current.id)
                {
                    body_interface.SetShape(rb.body_id, current.shape, true, activation_for(rb.type));
                    rb.applied_shape_id = current.id;
                }

                continue;
            }

            const collider_shape_t collider = build_collider_shape(reg, entity);

            JPH::BodyCreationSettings settings(
                collider.shape,
                JPH::Vec3(transform.world_mat[3][0], transform.world_mat[3][1], transform.world_mat[3][2]),
                JPH::Quat(transform.local_rotation.x, transform.local_rotation.y, transform.local_rotation.z,
                          transform.local_rotation.w),
                motion_type_for(rb.type), object_layer_for(rb.type));

            settings.mIsSensor = rb.is_sensor;

            rb.body_id = body_interface.CreateAndAddBody(settings, activation_for(rb.type));
            rb.applied_type = rb.type;
            rb.applied_is_sensor = rb.is_sensor;
            rb.applied_shape_id = collider.id;
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
} // namespace tau