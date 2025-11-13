#pragma once

#include "component.h"
#include "level.h"
#include "smol/components/transform.h"
#include "smol/defines.h"

#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>

namespace smol::core
{
    using comp_iterator_t = std::unordered_map<std::type_index, std::unique_ptr<component_t>>::const_iterator;
    using children_iterator_t = std::vector<std::shared_ptr<gameobject_t>>::iterator;

    class SMOL_API gameobject_t : public std::enable_shared_from_this<gameobject_t>
    {
      public:
        explicit gameobject_t(std::string_view name) : id(generate_id()), name(name), active(true) {}
        ~gameobject_t() = default;

        i32_t get_id() const { return id; }

        const std::string& get_name() const { return name; }
        void set_name(std::string_view new_name);

        bool is_active() const { return active; }
        void set_active(bool state);

        const std::shared_ptr<level_t> get_level() const { return owner_level.lock(); }
        void set_level(const std::shared_ptr<level_t>& level);

        const std::shared_ptr<gameobject_t> get_parent() const { return parent.lock(); }
        void set_parent(const std::shared_ptr<gameobject_t>& new_parent);
        const std::vector<std::shared_ptr<gameobject_t>>& get_children() const { return children; }
        void add_child(const std::shared_ptr<gameobject_t>& child);
        void remove_child(const std::shared_ptr<gameobject_t>& child);

        smol::components::transform_ct* get_transform() { return &transform; }
        const smol::components::transform_ct& get_transform() const { return transform; }

        template<typename T, typename... Args>
        inline T* add_component(Args&&... args)
        {
            static_assert(!std::is_same_v<T, smol::components::transform_ct>, "Transform is built-in and cannot be added as a component.");
            std::unique_ptr<T> new_comp = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw_ptr = new_comp.get();
            components[std::type_index(typeid(T))] = std::move(new_comp);

            owner_level.lock()->register_component(raw_ptr);
            raw_ptr->set_owner(this->shared_from_this());
            return raw_ptr;
        }

        template<typename T>
        T* get_component() const
        {
            comp_iterator_t comp_iterator = components.find(std::type_index(typeid(T)));
            if (comp_iterator != components.end())
            {
                return static_cast<T*>(comp_iterator->second.get());
            }

            for (const auto& [type, comp_ptr] : components)
            {
                if (auto casted = dynamic_cast<T*>(comp_ptr.get())) { return casted; }
            }

            return nullptr;
        }

        template<typename T>
        T* get_component_in_children() const
        {
            for (const std::shared_ptr<gameobject_t>& child : children)
            {
                T* comp = child->get_component<T>();
                if (comp != nullptr) { return comp; }

                comp = child->get_component_in_children<T>();
                if (comp != nullptr) { return comp; }
            }

            return nullptr;
        }

      private:
        i32_t id;
        std::string name;
        bool active;
        smol::components::transform_ct transform;

        std::weak_ptr<level_t> owner_level;
        std::weak_ptr<gameobject_t> parent;
        std::vector<std::shared_ptr<gameobject_t>> children;

        std::unordered_map<std::type_index, std::unique_ptr<component_t>> components;

        i32_t generate_id();
    };
} // namespace smol::core