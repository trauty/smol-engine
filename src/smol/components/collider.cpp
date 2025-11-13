#include "collider.h"

#include "smol/components/rigidbody.h"
#include "smol/components/transform.h"
#include "smol/core/gameobject.h"
#include "smol/log.h"
#include "smol/physics.h"

namespace smol::components
{
    void collider_ct::start()
    {
        if (!get_gameobject()->get_component<rigidbody_ct>())
        {
            transform_ct* transform = get_gameobject()->get_transform();
            vec3_t pos = transform->get_world_position();
            quat_t rot = transform->get_world_rotation();

            JPH::BodyCreationSettings settings(
                shape, JPH::Vec3Arg(pos.x, pos.y, pos.z), JPH::QuatArg(rot.x, rot.y, rot.z, rot.w), JPH::EMotionType::Static, static_cast<JPH::ObjectLayer>(layer_id));

            SMOL_LOG_DEBUG("COLLIDER", "Creating static body");
            JPH::Body* body = smol::physics::get_body_interface().CreateBody(settings);
            smol::physics::get_body_interface().AddBody(body->GetID(), JPH::EActivation::DontActivate);
            static_body_id = body->GetID();
        }
    }

    JPH::Shape* collider_ct::get_shape() const
    {
        return shape;
    }

    void collider_ct::set_layer(const std::string& name)
    {
        layer_id = smol::physics::get_layer_id(name);
        SMOL_LOG_DEBUG("COLLIDER", "Layer ID: {}", layer_id);
    }

    smol::physics::layer_id_t collider_ct::get_layer_id() const
    {
        return layer_id;
    }
} // namespace smol::components