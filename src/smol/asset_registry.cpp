#include "asset_registry.h"

namespace smol
{
    static std::atomic<size_t> type_counter{0};

    size_t get_next_asset_type_id() { return type_counter++; }
} // namespace smol