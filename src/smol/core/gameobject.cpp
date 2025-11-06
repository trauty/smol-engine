#include "gameobject.h"

#include <algorithm>
#include <atomic>

namespace smol::core
{
    void gameobject_t::set_name(std::string_view new_name)
    {
        name = new_name;
    }

    void gameobject_t::set_active(bool state)
    {
        active = state;
    }

    void gameobject_t::set_level(const std::shared_ptr<level_t>& level)
    {
        owner_level = level;
    }

    void gameobject_t::add_child(const std::shared_ptr<gameobject_t>& child)
    {
        if (!child || child.get() == this)
            return;

        for (std::shared_ptr<gameobject_t> ancestor = shared_from_this(); ancestor; ancestor = ancestor->get_parent())
        {
            if (ancestor == child)
                return;
        }

        if (std::find(children.begin(), children.end(), child) == children.end())
        {
            children.push_back(child);
        }

        if (child->get_parent().get() != this)
        {
            child->parent = shared_from_this();
        }
    }

    void gameobject_t::remove_child(const std::shared_ptr<gameobject_t>& child)
    {
        if (!child)
            return;

        children_iterator_t it = std::find(children.begin(), children.end(), child);
        if (it != children.end())
        {
            children.erase(it);

            if (child->get_parent().get() == this)
            {
                child->set_parent(nullptr);
            }
        }
    }

    void gameobject_t::set_parent(const std::shared_ptr<gameobject_t>& new_parent)
    {
        if (new_parent.get() == this)
            return;

        for (std::shared_ptr<gameobject_t> p = new_parent; p; p = p->get_parent())
        {
            if (p.get() == this)
                return;
        }

        if (const std::shared_ptr<gameobject_t>& old_parent = parent.lock())
        {
            old_parent->remove_child(shared_from_this());
        }

        parent = new_parent;

        if (new_parent)
        {
            new_parent->add_child(shared_from_this());
        }
    }

    static std::atomic<i32_t> instance_id_counter = 0;

    i32_t gameobject_t::generate_id()
    {
        return instance_id_counter.fetch_add(1, std::memory_order_relaxed);
    }
} // namespace smol::core