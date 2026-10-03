#include "tau/components/post_process.h"

#include "tau/assets/material.h"
#include "tau/engine.h"

namespace tau
{
    void post_process_t::on_added()
    {
        if (final_pass.is_valid()) { return; }

        final_pass = tau::engine::get_asset_registry().load_sync<material_t>(DEFAULT_FINAL_PASS_PATH);
    }
} // namespace tau
