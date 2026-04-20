#include "reflection.h"

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
            .func<&get_component<tag_t>>("get_component"_h)
            .func<&add_component<tag_t>>("add"_h)
            .func<&remove_component<tag_t>>("remove"_h)
            .data<&tag_t::name>("name"_h)
            .custom<editor_prop_t>("Entity Name");

        factory<transform_t>{}
            .type("transform_t"_h)
            .custom<editor_prop_t>("Transform")
            .func<&get_component<transform_t>>("get_component"_h)
            .func<&add_component<transform_t>>("add"_h)
            .func<&remove_component<transform_t>>("remove"_h)
            .func<&mark_transform_dirty>("on_changed"_h)
            .data<&transform_t::local_position>("local_position"_h)
            .custom<editor_prop_t>("Position")
            .data<&transform_t::local_scale>("local_scale"_h)
            .custom<editor_prop_t>("Scale");
    }
} // namespace smol::reflection