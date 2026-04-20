#pragma once

#include "entt/locator/locator.hpp"
#include "entt/meta/context.hpp"
#include "entt/meta/factory.hpp"
#include "entt/meta/meta.hpp"
#include "entt/meta/resolve.hpp"
#include "smol/ecs.h"

namespace smol::reflection
{
    using any_t = entt::meta_any;
    using type_t = entt::meta_type;
    using data_t = entt::meta_data;
    using custom_t = entt::meta_custom;
    using func_t = entt::meta_func;
    using ctx_t = entt::meta_ctx;

    template <typename T>
    using factory = entt::meta_factory<T>;

    using entt::forward_as_meta;
    using entt::resolve;

    using namespace entt::literals;

    template <typename T>
    any_t get_component(smol::ecs::registry_t& reg, smol::ecs::entity_t entity)
    {
        if (reg.all_of<T>(entity)) { return any_t{std::in_place_type<T&>, reg.get<T>(entity)}; }
        return {};
    }

    template <typename T>
    void add_component(smol::ecs::registry_t& reg, smol::ecs::entity_t entity)
    { reg.emplace_or_replace<T>(entity); }

    template <typename T>
    void remove_component(smol::ecs::registry_t& reg, smol::ecs::entity_t entity)
    { reg.remove<T>(entity); }

    struct editor_prop_t
    {
        const char* name = "Unknown";

        editor_prop_t(const char* n) : name(n) {}
    };

    void register_types();

    inline ctx_t& get_engine_context() { return entt::locator<entt::meta_ctx>::value_or(); }
} // namespace smol::reflection