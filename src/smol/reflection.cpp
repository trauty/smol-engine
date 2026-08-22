#include "reflection.h"

#include "smol/asset.h"
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
#include <vector>

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

    namespace
    {
#define SMOL_PIN_MEMBER(ptr, lit) static_assert(hash_string(member_name<ptr>()) == lit##_h, "reflection id drift: " lit)
#define SMOL_PIN_TYPE(T, lit) static_assert(hash_string(type_name<T>()) == lit##_h, "reflection id drift: " lit)
#define SMOL_PIN_ENUM(v, lit) static_assert(hash_string(enum_name<v>()) == lit##_h, "reflection id drift: " lit)

        SMOL_PIN_TYPE(tag_t, "tag_t");
        SMOL_PIN_MEMBER(&tag_t::name, "name");

        SMOL_PIN_TYPE(transform_t, "transform_t");
        SMOL_PIN_MEMBER(&transform_t::local_position, "local_position");
        SMOL_PIN_MEMBER(&transform_t::local_scale, "local_scale");

        SMOL_PIN_TYPE(active_camera_tag, "active_camera_tag");

        SMOL_PIN_TYPE(camera_t, "camera_t");
        SMOL_PIN_MEMBER(&camera_t::fov_deg, "fov_deg");
        SMOL_PIN_MEMBER(&camera_t::near_plane, "near_plane");
        SMOL_PIN_MEMBER(&camera_t::far_plane, "far_plane");
        SMOL_PIN_MEMBER(&camera_t::aspect, "aspect");

        SMOL_PIN_TYPE(directional_light_t, "directional_light_t");
        SMOL_PIN_TYPE(point_light_t, "point_light_t");
        SMOL_PIN_TYPE(spot_light_t, "spot_light_t");
        SMOL_PIN_MEMBER(&directional_light_t::color, "color");
        SMOL_PIN_MEMBER(&directional_light_t::intensity, "intensity");
        SMOL_PIN_MEMBER(&point_light_t::radius, "radius");
        SMOL_PIN_MEMBER(&spot_light_t::inner_angle, "inner_angle");
        SMOL_PIN_MEMBER(&spot_light_t::outer_angle, "outer_angle");

        SMOL_PIN_TYPE(body_type_e, "body_type_e");
        SMOL_PIN_ENUM(body_type_e::STATIC, "STATIC");
        SMOL_PIN_ENUM(body_type_e::KINEMATIC, "KINEMATIC");
        SMOL_PIN_ENUM(body_type_e::DYNAMIC, "DYNAMIC");

        SMOL_PIN_TYPE(rigidbody_t, "rigidbody_t");
        SMOL_PIN_MEMBER(&rigidbody_t::type, "type");
        SMOL_PIN_MEMBER(&rigidbody_t::is_sensor, "is_sensor");

        SMOL_PIN_TYPE(box_collider_t, "box_collider_t");
        SMOL_PIN_MEMBER(&box_collider_t::extents, "extents");
        SMOL_PIN_MEMBER(&box_collider_t::offset, "offset");

        SMOL_PIN_TYPE(sphere_collider_t, "sphere_collider_t");
        SMOL_PIN_MEMBER(&sphere_collider_t::radius, "radius");

        SMOL_PIN_TYPE(mesh_renderer_t, "mesh_renderer_t");
        SMOL_PIN_MEMBER(&mesh_renderer_t::mesh, "mesh");
        SMOL_PIN_MEMBER(&mesh_renderer_t::material, "material");
        SMOL_PIN_MEMBER(&mesh_renderer_t::active, "active");
        SMOL_PIN_MEMBER(&mesh_renderer_t::casts_shadow, "casts_shadow");

#undef SMOL_PIN_MEMBER
#undef SMOL_PIN_TYPE
#undef SMOL_PIN_ENUM
    } // namespace

    namespace
    {
        std::vector<register_func_t>& registrations()
        {
            static std::vector<register_func_t> list;
            return list;
        }
    } // namespace

    void add_registration(register_func_t fn)
    {
        if (fn != nullptr) { registrations().push_back(fn); }
    }

    void run_registrations(ctx_t& ctx)
    {
        for (register_func_t fn : registrations()) { fn(ctx); }
    }

    void clear_registrations() { registrations().clear(); }

    void register_types() { register_types_into(get_engine_context()); }

    void register_types_into(ctx_t& ctx)
    {
        factory<std::string>(ctx).type("std::string"_h, "std::string");
        // i32_t/u32_t are the same types as i32/u32 -- registering both just overwrites
        factory<i32>(ctx).type("i32"_h, "i32");
        factory<u32>(ctx).type("u32"_h, "u32");
        factory<bool>(ctx).type("bool"_h, "bool");

        factory<f32>(ctx).type("f32"_h, "f32");
        factory<asset_handle_t>(ctx).type("asset_handle_t"_h, "asset_handle_t");

        factory<vec3_t>{ctx}
            .type("vec3_t"_h, "vec3_t")
            .data<&vec3_t::x>("x"_h)
            .data<&vec3_t::y>("y"_h)
            .data<&vec3_t::z>("z"_h);

        component<tag_t>(ctx, "Tag").field<&tag_t::name>("Entity Name");

        component<transform_t>(ctx, "Transform")
            .field<&transform_t::local_position>("Position")
            .accessor<&set_transform_rot_euler, &get_transform_rot_euler>("Rotation", unit_e::RADIANS,
                                                                          stable_id{"local_euler"})
            .field<&transform_t::local_scale>("Scale")
            .on_changed<&mark_transform_dirty>();

        component<active_camera_tag>(ctx, "Active Camera Tag");

        component<camera_t>(ctx, "Camera")
            .field<&camera_t::fov_deg>("FOV")
            .field<&camera_t::near_plane>("Near Plane")
            .field<&camera_t::far_plane>("Far Plane")
            .field<&camera_t::aspect>("Aspect Ratio");

        // lighting
        component<directional_light_t>(ctx, "Directional Light")
            .field<&directional_light_t::color>("Color")
            .field<&directional_light_t::intensity>("Intensity");

        component<point_light_t>(ctx, "Point Light")
            .field<&point_light_t::color>("Color")
            .field<&point_light_t::intensity>("Intensity")
            .field<&point_light_t::radius>("Radius");

        component<spot_light_t>(ctx, "Spot Light")
            .field<&spot_light_t::color>("Color")
            .field<&spot_light_t::intensity>("Intensity")
            .field<&spot_light_t::radius>("Radius")
            .field<&spot_light_t::inner_angle>("Inner Angle")
            .field<&spot_light_t::outer_angle>("Outer Angle");

        // physics
        enumeration<body_type_e>(ctx, "Body Type");

        component<rigidbody_t>(ctx, "Rigidbody")
            .field<&rigidbody_t::type>("Body Type")
            .field<&rigidbody_t::is_sensor>("Is Sensor");

        component<box_collider_t>(ctx, "Box Collider")
            .field<&box_collider_t::extents>("Extents")
            .field<&box_collider_t::offset>("Offset");

        component<sphere_collider_t>(ctx, "Sphere Collider").field<&sphere_collider_t::radius>("Radius");

        // rendering
        component<mesh_renderer_t>(ctx, "Mesh Renderer")
            .field_asset<&mesh_renderer_t::mesh, smol::mesh_t>("Mesh")
            .field_asset<&mesh_renderer_t::material, smol::material_t>("Material")
            .field<&mesh_renderer_t::active>("Active")
            .field<&mesh_renderer_t::casts_shadow>("Casts Shadow");
    }

    void shutdown() { entt::meta_reset(get_engine_context()); }
} // namespace smol::reflection