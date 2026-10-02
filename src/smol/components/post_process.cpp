#include "smol/components/post_process.h"

#include "smol/assets/material.h"
#include "smol/engine.h"

namespace smol
{
    void post_process_t::on_added()
    {
        if (final_pass.is_valid()) { return; }

        final_pass = smol::engine::get_asset_registry().load_sync<material_t>(DEFAULT_FINAL_PASS_PATH);
    }
} // namespace smol
