#include "level.h"

#include "smol/core/component.h"
#include "smol/core/gameobject.h"
#include "smol/log.h"

namespace smol::core
{
    namespace
    {
        using level_component_iter_t = std::unordered_map<std::type_index, std::vector<component_t*>>::iterator;
    }

    void level_t::start()
    {
        for (level_component_iter_t it = component_registry.begin(); it != component_registry.end(); ++it)
        {
            std::vector<component_t*>& components = it->second;
            for (size_t i = 0; i < components.size(); i++)
            {
                component_t* comp = components[i];
                comp->start();
            }
        }
    }

    void level_t::update(f64 delta_time)
    {
        for (level_component_iter_t it = component_registry.begin(); it != component_registry.end(); ++it)
        {
            std::vector<component_t*>& components = it->second;
            for (size_t i = 0; i < components.size(); i++)
            {
                component_t* comp = components[i];
                if (comp->is_active())
                {
                    comp->update(delta_time);
                }
            }
        }
    }

    void level_t::fixed_update(f64 fixed_timestep)
    {
        for (level_component_iter_t it = component_registry.begin(); it != component_registry.end(); ++it)
        {
            std::vector<component_t*>& components = it->second;
            for (size_t i = 0; i < components.size(); i++)
            {
                component_t* comp = components[i];
                if (comp->is_active())
                {
                    comp->fixed_update(fixed_timestep);
                }
            }
        }
    }

    std::shared_ptr<gameobject_t> level_t::create_gameobject(std::string_view name)
    {
        std::shared_ptr<gameobject_t> game_obj = std::make_shared<gameobject_t>(name);

        game_obj->set_level(shared_from_this());
        game_obj->get_transform()->set_owner(game_obj);

        gameobjects.push_back(game_obj);
        return game_obj;
    }

    void level_t::register_component(component_t* comp)
    {
        std::type_index type_index = typeid(*comp);

        level_component_iter_t it = component_registry.find(type_index);
        if (it == component_registry.end())
        {
            std::vector<component_t*> new_comp_list;
            new_comp_list.push_back(comp);
            component_registry[type_index] = new_comp_list;
        }
        else
        {
            it->second.push_back(comp);
        }
    }

    template<typename T>
    std::span<T*> level_t::get_all_components_of_type()
    {
        std::type_index type_index = typeid(T);

        level_component_iter_t it = component_registry.find(type_index);
        if (it != component_registry.end())
        {
            const std::vector<component_t*>& comp_list = it->second;
            return std::span<T*>(reinterpret_cast<T* const*>(comp_list.data()), comp_list.size());
        }

        return std::span<T*>();
    }
} // namespace smol::core