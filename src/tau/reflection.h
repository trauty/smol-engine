#pragma once

#include "entt/locator/locator.hpp"
#include "entt/meta/container.hpp"
#include "entt/meta/context.hpp"
#include "entt/meta/factory.hpp"
#include "entt/meta/meta.hpp"
#include "entt/meta/resolve.hpp"
#include "tau/defines.h"
#include "tau/ecs.h"
#include "tau/hash.h"
#include "tau/nameof.h"

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

namespace tau::reflection
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
    any_t get_component(tau::ecs::registry_t& reg, tau::ecs::entity_t entity)
    {
        if (reg.all_of<T>(entity))
        {
            if constexpr (std::is_empty_v<T>) { return any_t{std::in_place_type<T>}; }
            else
            {
                return any_t{std::in_place_type<T&>, reg.get<T>(entity)};
            }
        }
        return {};
    }

    template <typename T>
    void add_component(tau::ecs::registry_t& reg, tau::ecs::entity_t entity)
    {
        if constexpr (std::is_empty_v<T>) { reg.emplace_or_replace<T>(entity); }
        else
        {
            T& component = reg.emplace_or_replace<T>(entity);
            if constexpr (requires { component.on_added(); }) { component.on_added(); }
        }
    }

    template <typename T>
    void remove_component(tau::ecs::registry_t& reg, tau::ecs::entity_t entity)
    { reg.remove<T>(entity); }

    // creates T's pool from the module this is instantiated in, the engine for engine components
    // an entt pool keeps the type_info pointer of its creator, so a pool the game DLL created dangles after unload
    template <typename T>
    void ensure_storage(tau::ecs::registry_t& reg)
    { reg.storage<T>(); }

    enum class unit_e : u8_t
    {
        NONE,
        RADIANS,
    };

    struct editor_prop_t
    {
        const char* name = "Unknown";
        u64_t asset_type_hash = 0;
        unit_e unit = unit_e::NONE;

        bool is_list = false;

        editor_prop_t() = default;
        editor_prop_t(const char* n) : name(n) {}
        editor_prop_t(const char* n, u64_t asset_type) : name(n), asset_type_hash(asset_type) {}
        editor_prop_t(const char* n, u64_t asset_type, bool list) : name(n), asset_type_hash(asset_type), is_list(list)
        {
        }
        editor_prop_t(const char* n, unit_e u) : name(n), unit(u) {}
    };

    struct stable_id
    {
        const char* name;
        constexpr explicit stable_id(const char* n) : name(n) {}
    };

    template <typename T>
    class component_t
    {
      public:
        component_t(ctx_t& ctx, const char* label) : m_factory(ctx)
        {
            m_factory.type(tau::hash_string(tau::type_name<T>()), tau::type_label<T>.c_str())
                .template custom<editor_prop_t>(label)
                .template func<&get_component<T>>("get"_h)
                .template func<&add_component<T>>("add"_h)
                .template func<&remove_component<T>>("remove"_h)
                .template func<&ensure_storage<T>>("storage"_h);
        }

        template <auto MemberPtr>
        component_t& field(const char* label)
        {
            return bind_field<MemberPtr>(tau::hash_string(tau::member_name<MemberPtr>()),
                                         tau::member_label<MemberPtr>.c_str(), editor_prop_t{label});
        }

        template <auto MemberPtr>
        component_t& field(const char* label, stable_id id)
        { return bind_field<MemberPtr>(tau::hash_string(id.name), id.name, editor_prop_t{label}); }

        template <auto MemberPtr, typename AssetT>
        component_t& field_asset(const char* label)
        {
            return bind_field<MemberPtr>(tau::hash_string(tau::member_name<MemberPtr>()),
                                         tau::member_label<MemberPtr>.c_str(),
                                         editor_prop_t{label, tau::get_type_id<AssetT>()});
        }

        template <auto MemberPtr, typename AssetT>
        component_t& field_asset_list(const char* label)
        {
            return bind_field<MemberPtr>(tau::hash_string(tau::member_name<MemberPtr>()),
                                         tau::member_label<MemberPtr>.c_str(),
                                         editor_prop_t{label, tau::get_type_id<AssetT>(), true});
        }

        template <auto Setter, auto Getter>
        component_t& accessor(const char* label, stable_id id)
        {
            m_factory.template data<Setter, Getter>(tau::hash_string(id.name), id.name)
                .template custom<editor_prop_t>(editor_prop_t{label});
            return *this;
        }

        template <auto Setter, auto Getter>
        component_t& accessor(const char* label, unit_e unit, stable_id id)
        {
            m_factory.template data<Setter, Getter>(tau::hash_string(id.name), id.name)
                .template custom<editor_prop_t>(editor_prop_t{label, unit});
            return *this;
        }

        template <auto Fn>
        component_t& on_changed()
        {
            m_factory.template func<Fn>("on_changed"_h);
            return *this;
        }

        factory<T>& raw() { return m_factory; }

      private:
        template <auto MemberPtr>
        component_t& bind_field(u32_t id, const char* name, editor_prop_t prop)
        {
            m_factory.template data<MemberPtr>(id, name).template custom<editor_prop_t>(prop);
            return *this;
        }

        factory<T> m_factory;
    };

    template <typename T>
    component_t<T> component(ctx_t& ctx, const char* label)
    { return component_t<T>{ctx, label}; }

    template <typename E>
    class enumeration_t
    {
      public:
        enumeration_t(ctx_t& ctx, const char* label) : m_factory(ctx)
        {
            m_factory.type(tau::hash_string(tau::type_name<E>()), tau::type_label<E>.c_str())
                .template custom<editor_prop_t>(label);
        }

        template <auto EnumValue>
        enumeration_t& value()
        {
            static_assert(!tau::enum_name<EnumValue>().empty(), "not an enumerator of this type");

            m_factory
                .template data<EnumValue>(tau::hash_string(tau::enum_name<EnumValue>()),
                                          tau::enum_label<EnumValue>.c_str())
                .template custom<editor_prop_t>(tau::enum_label<EnumValue>.c_str());
            return *this;
        }

        template <std::size_t Limit = 64>
        enumeration_t& discover()
        {
            discover_range(std::make_index_sequence<Limit>{});
            return *this;
        }

      private:
        template <std::size_t... Is>
        void discover_range(std::index_sequence<Is...>)
        { (try_value<static_cast<E>(Is)>(), ...); }

        template <auto EnumValue>
        void try_value()
        {
            if constexpr (!tau::enum_name<EnumValue>().empty()) { value<EnumValue>(); }
        }

        factory<E> m_factory;
    };

    template <typename E, std::size_t Limit = 64>
    enumeration_t<E> enumeration(ctx_t& ctx, const char* label)
    {
        enumeration_t<E> builder{ctx, label};
        builder.template discover<Limit>();
        return builder;
    }

    using register_func_t = void (*)(ctx_t&);

    TAU_ENGINE_API void add_registration(register_func_t fn);

    TAU_ENGINE_API void run_registrations(ctx_t& ctx);

    TAU_ENGINE_API void clear_registrations();

    struct registrar_t
    {
        explicit registrar_t(register_func_t fn) { add_registration(fn); }
    };

    TAU_ENGINE_API void register_types();
    TAU_ENGINE_API void register_types_into(ctx_t& ctx);

    TAU_ENGINE_API void shutdown();

    inline ctx_t& get_engine_context() { return entt::locator<entt::meta_ctx>::value_or(); }
} // namespace tau::reflection

#define TAU_REFLECT_JOIN_INNER(a, b) a##b
#define TAU_REFLECT_JOIN(a, b) TAU_REFLECT_JOIN_INNER(a, b)

#define TAU_REFLECT() TAU_REFLECT_BLOCK(__COUNTER__)

#define TAU_REFLECT_BLOCK(id)                                                                                          \
    static void TAU_REFLECT_JOIN(tau_reflect_fn_, id)(::tau::reflection::ctx_t&);                                      \
    static const ::tau::reflection::registrar_t TAU_REFLECT_JOIN(tau_reflect_reg_,                                     \
                                                                 id){&TAU_REFLECT_JOIN(tau_reflect_fn_, id)};          \
    static void TAU_REFLECT_JOIN(tau_reflect_fn_, id)([[maybe_unused]] ::tau::reflection::ctx_t & ctx)
