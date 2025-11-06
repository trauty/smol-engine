#include "component.h"

#include "gameobject.h"

namespace smol::core
{
    void component_t::set_owner(const std::shared_ptr<gameobject_t>& new_owner)
    {
        owner = new_owner;
    }
} // namespace smol::core