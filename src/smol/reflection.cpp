#include "reflection.h"

#include "smol/assets/material.h"
#include "smol/assets/mesh.h"
#include "smol/components/camera.h"
#include "smol/components/lighting.h"
#include "smol/components/physics.h"
#include "smol/components/renderer.h"
#include "smol/components/tag.h"
#include "smol/components/transform.h"
#include "smol/ecs_fwd.h"
#include "smol/hash.h"
#include "smol/math.h"

#include <entt/entt.hpp>
#include <entt/meta/meta.hpp>
#include <string>

namespace smol::reflection
{
    void mark_transform_dirty(smol::ecs::registry_t& reg, smol::ecs::entity_t entity)
    { reg.get<transform_t>(entity).is_dirty = true; }

    vec3_t get_transform_rot_euler(const smol::transform_t& transform)
    { return quat_t::to_euler(transform.local_rotation); }

    void set_transform_rot_euler(smol::transform_t& transform, const vec3_t& euler)
    {
        transform.local_rotation = quat_t::from_euler(euler);
        transform.is_dirty = true;
    }

    void register_types()
    {
        factory<std::string>().type("std::string"_h);
        factory<i32_t>().type("i32_t"_h);
        factory<u32_t>().type("u32_t"_h);
        factory<i32>().type("i32"_h);
        factory<u32>().type("u32"_h);
        factory<bool>().type("bool"_h);

        factory<vec3_t>{}.type("vec3_t"_h).data<&vec3_t::x>("x"_h).data<&vec3_t::y>("y"_h).data<&vec3_t::z>("z"_h);

        factory<tag_t>{}
            .type("tag_t"_h)
            .custom<editor_prop_t>("Tag")
            .func<&get_component<tag_t>>("get"_h)
            .func<&add_component<tag_t>>("add"_h)
            .func<&remove_component<tag_t>>("remove"_h)
            .data<&tag_t::name>("name"_h)
            .custom<editor_prop_t>("Entity Name");

        factory<transform_t>{}
            .type("transform_t"_h)
            .custom<editor_prop_t>("Transform")
            .func<&get_component<transform_t>>("get"_h)
            .func<&add_component<transform_t>>("add"_h)
            .func<&remove_component<transform_t>>("remove"_h)
            .func<&mark_transform_dirty>("on_changed"_h)
            .data<&transform_t::local_position>("local_position"_h)
            .custom<editor_prop_t>("Position")
            .data<&set_transform_rot_euler, &get_transform_rot_euler>("local_euler"_h)
            .custom<editor_prop_t>("Rotation")
            .data<&transform_t::local_scale>("local_scale"_h)
            .custom<editor_prop_t>("Scale");

        factory<smol::active_camera_tag>{}
            .type("active_camera_tag"_h)
            .custom<editor_prop_t>("Active Camera Tag")
            .func<&get_component<smol::active_camera_tag>>("get"_h)
            .func<&add_component<smol::active_camera_tag>>("add"_h)
            .func<&remove_component<smol::active_camera_tag>>("remove"_h);

        factory<smol::camera_t>{}
            .type("camera_t"_h)
            .custom<editor_prop_t>("Camera")
            .func<&get_component<smol::camera_t>>("get"_h)
            .func<&add_component<smol::camera_t>>("add"_h)
            .func<&remove_component<smol::camera_t>>("remove"_h)
            .data<&smol::camera_t::fov_deg>("fov_deg"_h)
            .custom<editor_prop_t>("FOV")
            .data<&smol::camera_t::near_plane>("near_plane"_h)
            .custom<editor_prop_t>("Near Plane")
            .data<&smol::camera_t::far_plane>("far_plane"_h)
            .custom<editor_prop_t>("Far Plane")
            .data<&smol::camera_t::aspect>("aspect"_h)
            .custom<editor_prop_t>("Aspect Ratio");

        // lighting
        factory<smol::directional_light_t>{}
            .type("directional_light_t"_h)
            .custom<editor_prop_t>("Directional Light")
            .func<&get_component<smol::directional_light_t>>("get"_h)
            .func<&add_component<smol::directional_light_t>>("add"_h)
            .func<&remove_component<smol::directional_light_t>>("remove"_h)
            .data<&smol::directional_light_t::color>("color"_h)
            .custom<editor_prop_t>("Color")
            .data<&smol::directional_light_t::intensity>("intensity"_h)
            .custom<editor_prop_t>("Intensity");

        factory<smol::point_light_t>{}
            .type("point_light_t"_h)
            .custom<editor_prop_t>("Point Light")
            .func<&get_component<smol::point_light_t>>("get"_h)
            .func<&add_component<smol::point_light_t>>("add"_h)
            .func<&remove_component<smol::point_light_t>>("remove"_h)
            .data<&smol::point_light_t::color>("color"_h)
            .custom<editor_prop_t>("Color")
            .data<&smol::point_light_t::intensity>("intensity"_h)
            .custom<editor_prop_t>("Intensity")
            .data<&smol::point_light_t::radius>("radius"_h)
            .custom<editor_prop_t>("Radius");

        factory<smol::spot_light_t>{}
            .type("spot_light_t"_h)
            .custom<editor_prop_t>("Spot Light")
            .func<&get_component<smol::spot_light_t>>("get"_h)
            .func<&add_component<smol::spot_light_t>>("add"_h)
            .func<&remove_component<smol::spot_light_t>>("remove"_h)
            .data<&smol::spot_light_t::color>("color"_h)
            .custom<editor_prop_t>("Color")
            .data<&smol::spot_light_t::intensity>("intensity"_h)
            .custom<editor_prop_t>("Intensity")
            .data<&smol::spot_light_t::radius>("radius"_h)
            .custom<editor_prop_t>("Radius")
            .data<&smol::spot_light_t::inner_angle>("inner_angle"_h)
            .custom<editor_prop_t>("Inner Angle")
            .data<&smol::spot_light_t::outer_angle>("outer_angle"_h)
            .custom<editor_prop_t>("Outer Angle");

        // physics
        factory<smol::body_type_e>{}
            .type("body_type_e"_h)
            .custom<editor_prop_t>("Body Type")
            .data<smol::body_type_e::STATIC>("STATIC"_h)
            .data<smol::body_type_e::KINEMATIC>("KINEMATIC"_h)
            .data<smol::body_type_e::DYNAMIC>("DYNAMIC"_h);

        factory<smol::rigidbody_t>{}
            .type("rigidbody_t"_h)
            .custom<editor_prop_t>("Rigidbody")
            .func<&get_component<smol::rigidbody_t>>("get"_h)
            .func<&add_component<smol::rigidbody_t>>("add"_h)
            .func<&remove_component<smol::rigidbody_t>>("remove"_h)
            .data<&smol::rigidbody_t::type>("type"_h)
            .custom<editor_prop_t>("Body Type")
            .data<&smol::rigidbody_t::is_sensor>("is_sensor"_h)
            .custom<editor_prop_t>("Is Sensor");

        factory<smol::box_collider_t>{}
            .type("box_collider_t"_h)
            .custom<editor_prop_t>("Box Collider")
            .func<&get_component<smol::box_collider_t>>("get"_h)
            .func<&add_component<smol::box_collider_t>>("add"_h)
            .func<&remove_component<smol::box_collider_t>>("remove"_h)
            .data<&smol::box_collider_t::extents>("extents"_h)
            .custom<editor_prop_t>("Extents")
            .data<&smol::box_collider_t::offset>("offset"_h)
            .custom<editor_prop_t>("Offset");

        factory<smol::sphere_collider_t>{}
            .type("sphere_collider_t"_h)
            .custom<editor_prop_t>("Sphere Collider")
            .func<&get_component<smol::sphere_collider_t>>("get"_h)
            .func<&add_component<smol::sphere_collider_t>>("add"_h)
            .func<&remove_component<smol::sphere_collider_t>>("remove"_h)
            .data<&smol::sphere_collider_t::radius>("radius"_h)
            .custom<editor_prop_t>("Radius");

        // rendering
        // factory<smol::asset_t<smol::mesh_t>>{}.type("asset_mesh"_h);
        // factory<smol::asset_t<smol::material_t>>{}.type("asset_material"_h);

        factory<smol::mesh_renderer_t>{}
            .type("mesh_renderer_t"_h)
            .custom<editor_prop_t>("Mesh Renderer")
            .func<&get_component<smol::mesh_renderer_t>>("get"_h)
            .func<&add_component<smol::mesh_renderer_t>>("add"_h)
            .func<&remove_component<smol::mesh_renderer_t>>("remove"_h)
            .data<&smol::mesh_renderer_t::mesh>("mesh"_h)
            .custom<editor_prop_t>("Mesh")
            .data<&smol::mesh_renderer_t::material>("material"_h)
            .custom<editor_prop_t>("Material")
            .data<&smol::mesh_renderer_t::active>("active"_h)
            .custom<editor_prop_t>("Active");
    }
} // namespace smol::reflection