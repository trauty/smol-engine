#include "rigidbody.h"

#include "collider.h"
#include "smol/components/transform.h"
#include "smol/core/gameobject.h"
#include "smol/log.h"
#include "smol/physics.h"

#include <Jolt/Physics/Body/AllowedDOFs.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

namespace smol::components
{
    rigidbody_ct::~rigidbody_ct()
    {
        if (body)
        {
            smol::physics::get_body_interface().RemoveBody(body_id);
            smol::physics::get_body_interface().DestroyBody(body_id);
        }
    }

    void rigidbody_ct::start()
    {
        std::shared_ptr<smol::core::gameobject_t> go = get_gameobject();
        if (!go)
        {
            SMOL_LOG_ERROR("RIGIDBODY", "No Gameobject assigned");
            return;
        }

        collider_ct* collider = go->get_component<collider_ct>();
        if (!collider)
        {
            SMOL_LOG_ERROR("RIGIDBODY", "No Collider assigned");
            return;
        }

        transform_ct* transform = go->get_transform();
        const vec3_t pos = transform->get_world_position();
        const quat_t rot = transform->get_world_rotation();

        JPH::BodyCreationSettings settings(
            collider->get_shape(),
            JPH::Vec3(pos.x, pos.y, pos.z),
            JPH::Quat(rot.x, rot.y, rot.z, rot.w),
            is_kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic,
            static_cast<JPH::ObjectLayer>(collider->get_layer_id()));

        if (locked_x)
            settings.mAllowedDOFs &= ~JPH::EAllowedDOFs::RotationX;
        if (locked_y)
            settings.mAllowedDOFs &= ~JPH::EAllowedDOFs::RotationY;
        if (locked_z)
            settings.mAllowedDOFs &= ~JPH::EAllowedDOFs::RotationZ;

        body = smol::physics::get_body_interface().CreateBody(settings);
        if (!body)
        {
            SMOL_LOG_ERROR("RIGIDBODY", "Could not create body");
            return;
        }

        body_id = body->GetID();
        smol::physics::get_body_interface().AddBody(body_id, JPH::EActivation::Activate);
    }

    void rigidbody_ct::update([[maybe_unused]] f64 delta_time)
    {
        // interpolation
    }

    void rigidbody_ct::fixed_update([[maybe_unused]] f64 fixed_timestep)
    {
        if (!body)
            return;

        transform_ct* transform = get_gameobject()->get_transform();
        const JPH::RVec3 position = body->GetPosition();
        const JPH::Quat rotation = body->GetRotation();
        vec3_t new_pos = {position.GetX(), position.GetY(), position.GetZ()};
        quat_t new_rot = {rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW()};

        transform->set_world_position(new_pos);
        transform->set_world_rotation(new_rot);
    }

    void rigidbody_ct::add_force(const vec3_t& force)
    {
        if (!body || is_kinematic)
        {
            SMOL_LOG_WARN("RIGIDBODY", "add_force: Body is kinematic or doesn't exist");
            return;
        }

        smol::physics::get_body_interface().AddForce(body_id, JPH::Vec3(force.x, force.y, force.z));
    }

    void rigidbody_ct::lock_axis(bool x_axis, bool y_axis, bool z_axis)
    {
        locked_x = x_axis;
        locked_y = y_axis;
        locked_z = z_axis;
    }
} // namespace smol::components