#pragma once

#include "smol/defines.h"

#include <memory>
#include <span>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace smol::core
{
    class gameobject_t;
    class component_t;

    class SMOL_API level_t : public std::enable_shared_from_this<level_t>
    {
      public:
        level_t(std::string_view name) : name(name) {}

        void start();
        void update(f64 delta_time);
        void fixed_update(f64 fixed_timestep);

        std::shared_ptr<gameobject_t> create_gameobject(std::string_view name);

        void register_component(component_t* comp);

        template<typename T>
        std::span<T*> get_all_components_of_type();

      private:
        std::string name;

        std::vector<std::shared_ptr<gameobject_t>> gameobjects;
        std::unordered_map<std::type_index, std::vector<component_t*>> component_registry;
    };
} // namespace smol::core