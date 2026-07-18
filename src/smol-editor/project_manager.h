#pragma once

#include <string>

namespace smol { struct editor_context_t; }

namespace smol::editor::project_manager
{
    bool draw(smol::editor_context_t& ctx, std::string& out_project_file, bool* p_open);
    void add_recent(const std::string& project_file);
} // namespace smol::editor::project_manager
