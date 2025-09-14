#pragma once

#include "smol/defines.h"

#include <vector>

#include "smol/core/component.h"
#include "smol/math_util.h"

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
}