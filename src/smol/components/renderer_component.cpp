#include "renderer_component.h"

#include <algorithm>

namespace smol::components
{
    std::vector<renderer_ct*> renderer_ct::all_renderers;

    renderer_ct::renderer_ct()
    {
        all_renderers.push_back(this);
    }

    renderer_ct::~renderer_ct()
    {
        std::vector<renderer_ct*>::const_iterator it = std::find(all_renderers.begin(), all_renderers.end(), this);
        if (it != all_renderers.end())
        {
            all_renderers.erase(it);
        }
    }
} // namespace smol::components