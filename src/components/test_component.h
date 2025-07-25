#pragma once

#include "defines.h"

#include "core/component.h"

namespace smol::components
{
    class SMOL_API test_component_t : public smol::core::component_t
    {
    public:
        void start();
        void update(f64 delta_time);
    };
}