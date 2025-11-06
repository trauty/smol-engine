#pragma once

#include "smol/core/component.h"
#include "smol/defines.h"
#include "smol/math_util.h"

#include <vector>

namespace smol::components
{
    class renderer_ct : public smol::core::component_t
    {
      public:
        static std::vector<renderer_ct*> all_renderers;

        renderer_ct();
        ~renderer_ct();
        virtual void render() const = 0;
    };
} // namespace smol::components