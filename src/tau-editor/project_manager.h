#pragma once

#include <string>

namespace tau { struct editor_context_t; }

namespace tau::editor::project_manager
{
    bool draw(tau::editor_context_t& ctx, std::string& out_project_file, bool* p_open);

    // full window wait screen while a project builds and cooks, `line` names the step, the log shows the work
    // returns true the frame Cancel is pressed, `cancelling` greys the button out afterwards
    bool draw_waiting(const char* activity, const std::string& line, bool cancelling);

    void report_status(const std::string& message);

    std::string engine_dir();

    void add_recent(const std::string& project_file);
} // namespace tau::editor::project_manager
