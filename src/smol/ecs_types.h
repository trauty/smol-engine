#pragma once

#include "smol/defines.h"

namespace smol::ecs
{
    using entity_t = u32_t;
    constexpr entity_t NULL_ENTITY = std::numeric_limits<entity_t>::max();

#define SMOL_COMPONENT(Name)                                                                                           \
    static constexpr std::string_view get_type_name() { return #Name; }

    template<typename T>
    concept is_component_con = requires {
        { T::get_type_name() } -> std::convertible_to<std::string_view>;
    };

    class registry_t;

} // namespace smol::ecs