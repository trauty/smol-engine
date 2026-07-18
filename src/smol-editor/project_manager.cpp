#include "smol-editor/project_manager.h"

#include "imgui/imgui.h"
#include "smol-editor/editor_context.h"
#include "smol/log.h"

#include "json/json.hpp"
#include <SDL3/SDL_filesystem.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace smol::editor::project_manager
{
    namespace
    {
        std::vector<std::string> g_recents;
        bool g_recents_loaded = false;

        std::string recents_file()
        {
            char* pref = SDL_GetPrefPath("smol-engine", "editor");
            if (!pref) { return ""; }
            std::string p = std::string(pref) + "recent_projects.json";
            SDL_free(pref);
            return p;
        }

        void load_recents()
        {
            g_recents_loaded = true;
            g_recents.clear();

            const std::string file = recents_file();
            if (file.empty() || !fs::exists(file)) { return; }

            std::ifstream in(file);
            nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
            if (j.is_discarded() || !j.is_array()) { return; }

            for (const auto& e : j)
            {
                if (e.is_string()) { g_recents.push_back(e.get<std::string>()); }
            }
        }

        void save_recents()
        {
            const std::string file = recents_file();
            if (file.empty()) { return; }

            nlohmann::json j = nlohmann::json::array();
            for (const auto& p : g_recents) { j.push_back(p); }

            std::ofstream out(file);
            out << j.dump(4);
        }

        std::string template_dir()
        {
            const char* base = SDL_GetBasePath();
            if (base)
            {
                std::string bp = base;
                while (bp.size() > 1 && (bp.back() == '/' || bp.back() == '\\')) { bp.pop_back(); }
                const fs::path sdk_tpl = fs::path(bp).parent_path() / "share" / "smol" / "template";
                if (fs::is_directory(sdk_tpl)) { return sdk_tpl.string(); }
            }

            const char* env = std::getenv("SMOL_ENGINE_DIR");
            if (env && *env)
            {
                const fs::path src_tpl = fs::path(env) / "template";
                if (fs::is_directory(src_tpl)) { return src_tpl.string(); }
                const fs::path sdk_tpl = fs::path(env) / "share" / "smol" / "template";
                if (fs::is_directory(sdk_tpl)) { return sdk_tpl.string(); }
            }

            return "";
        }

        bool resolve_project_arg(const std::string& arg, std::string& out)
        {
            if (arg.empty()) { return false; }
            std::error_code ec;
            const fs::path p = arg;

            if (fs::is_regular_file(p, ec) && p.extension() == ".smolproject")
            {
                out = fs::absolute(p, ec).string();
                return !ec;
            }
            if (fs::is_directory(p, ec))
            {
                fs::path found;
                int count = 0;
                for (auto it = fs::directory_iterator(p, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
                {
                    if (it->path().extension() == ".smolproject")
                    {
                        found = it->path();
                        ++count;
                    }
                }
                if (count == 1)
                {
                    out = fs::absolute(found, ec).string();
                    return !ec;
                }
            }
            return false;
        }

        bool valid_name(const std::string& name)
        {
            if (name.empty()) { return false; }
            return std::all_of(name.begin(), name.end(),
                               [](char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '-'; });
        }

        std::string instantiate_template(const std::string& name, const fs::path& out_dir, std::string& err)
        {
            const std::string tpl = template_dir();
            if (tpl.empty())
            {
                err = "Project template not found (is this a full SDK install, or is SMOL_ENGINE_DIR set?)";
                return "";
            }

            std::error_code ec;
            if (fs::exists(out_dir) && !fs::is_empty(out_dir, ec))
            {
                err = "Target directory exists and is not empty: " + out_dir.string();
                return "";
            }

            fs::create_directories(out_dir, ec);
            fs::copy(tpl, out_dir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                err = "Failed to copy template: " + ec.message();
                return "";
            }

            for (auto it = fs::recursive_directory_iterator(out_dir, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (!it->is_regular_file()) { continue; }
                const fs::path f = it->path();

                std::ifstream in(f, std::ios::binary);
                std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                in.close();

                std::string::size_type pos = 0;
                bool changed = false;
                const std::string token = "${NAME}";
                while ((pos = content.find(token, pos)) != std::string::npos)
                {
                    content.replace(pos, token.size(), name);
                    pos += name.size();
                    changed = true;
                }

                if (changed)
                {
                    std::ofstream out(f, std::ios::binary | std::ios::trunc);
                    out << content;
                }
            }

            const fs::path templated = out_dir / "${NAME}.smolproject";
            const fs::path final_proj = out_dir / (name + ".smolproject");
            if (fs::exists(templated)) { fs::rename(templated, final_proj, ec); }

            if (!fs::exists(final_proj))
            {
                err = "Template produced no .smolproject";
                return "";
            }

            return final_proj.string();
        }

        fs::path g_browse_dir;

        const char* user_home()
        {
            const char* home = std::getenv("HOME");
            if (!home || !*home) { home = std::getenv("USERPROFILE"); }
            return (home && *home) ? home : nullptr;
        }

        void ensure_browse_dir()
        {
            if (!g_browse_dir.empty()) { return; }
            const char* home = user_home();
            std::error_code ec;
            g_browse_dir = home ? fs::path(home) : fs::current_path(ec);
        }

        bool draw_browser(bool pick_project, std::string& out)
        {
            ensure_browse_dir();
            bool picked = false;

            ImGui::TextDisabled("%s", g_browse_dir.string().c_str());

            if (ImGui::BeginChild("browser", ImVec2(0, 220), ImGuiChildFlags_Borders))
            {
                std::error_code ec;
                if (g_browse_dir.has_parent_path() && g_browse_dir.parent_path() != g_browse_dir)
                {
                    if (ImGui::Selectable("../")) { g_browse_dir = g_browse_dir.parent_path(); }
                }

                std::vector<fs::path> dirs, projects;
                for (auto it = fs::directory_iterator(g_browse_dir, fs::directory_options::skip_permission_denied, ec);
                     !ec && it != fs::directory_iterator(); it.increment(ec))
                {
                    const fs::path& p = it->path();
                    const std::string fname = p.filename().string();
                    if (!fname.empty() && fname[0] == '.') { continue; }
                    if (it->is_directory(ec)) { dirs.push_back(p); }
                    else if (p.extension() == ".smolproject") { projects.push_back(p); }
                }
                std::sort(dirs.begin(), dirs.end());
                std::sort(projects.begin(), projects.end());

                for (const fs::path& d : dirs)
                {
                    const std::string label = d.filename().string() + "/";
                    if (ImGui::Selectable(label.c_str())) { g_browse_dir = d; }
                }
                if (pick_project)
                {
                    for (const fs::path& p : projects)
                    {
                        if (ImGui::Selectable(p.filename().string().c_str()))
                        {
                            out = p.string();
                            picked = true;
                        }
                    }
                }
            }
            ImGui::EndChild();

            return picked;
        }
    } // namespace

    void add_recent(const std::string& project_file)
    {
        if (!g_recents_loaded) { load_recents(); }

        std::error_code ec;
        const std::string abs = fs::absolute(project_file, ec).string();
        const std::string key = ec ? project_file : abs;

        g_recents.erase(std::remove(g_recents.begin(), g_recents.end(), key), g_recents.end());
        g_recents.insert(g_recents.begin(), key);
        if (g_recents.size() > 10) { g_recents.resize(10); }

        save_recents();
    }

    bool draw(smol::editor_context_t& ctx, std::string& out_project_file, bool* p_open)
    {
        (void)ctx;
        if (!g_recents_loaded) { load_recents(); }

        bool open_now = false;

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(ImVec2(560.0f, 520.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (!ImGui::Begin("Project Manager", p_open))
        {
            ImGui::End();
            return false;
        }

        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);
        ImGui::TextUnformatted("smol");
        ImGui::PopFont();
        ImGui::TextDisabled("open a project to start editing");
        ImGui::Separator();
        ImGui::Spacing();

        static std::string status;

        if (ImGui::CollapsingHeader("New Project", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static char name_buf[128] = "";
            static char loc_buf[1024] = "";
            if (loc_buf[0] == '\0')
            {
                const char* home = user_home();
                if (home) { std::snprintf(loc_buf, sizeof(loc_buf), "%s", home); }
            }

            ImGui::InputText("Name", name_buf, sizeof(name_buf));
            ImGui::InputText("Location", loc_buf, sizeof(loc_buf));

            if (ImGui::Button("Create##new"))
            {
                const std::string name = name_buf;
                if (!valid_name(name)) { status = "Invalid name (use A-Z a-z 0-9 _ -)"; }
                else
                {
                    std::string err;
                    const fs::path out_dir = fs::path(loc_buf) / name;
                    const std::string proj = instantiate_template(name, out_dir, err);
                    if (proj.empty()) { status = "New: " + err; }
                    else
                    {
                        add_recent(proj);
                        status = "Created " + proj + " -- build it (xmake) before opening";
                        name_buf[0] = '\0';
                    }
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(Scaffolds the template; build with xmake, then Open)");
        }

        if (ImGui::CollapsingHeader("Open Project", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static char path_buf[1024] = "";
            ImGui::InputTextWithHint("##openpath", "Path to a .smolproject (or its folder)", path_buf,
                                     sizeof(path_buf));
            ImGui::SameLine();
            if (ImGui::Button("Open##openpath"))
            {
                std::string resolved;
                if (!resolve_project_arg(path_buf, resolved)) { status = "No .smolproject at that path"; }
                else
                {
                    out_project_file = resolved;
                    open_now = true;
                }
            }

            std::string picked;
            if (draw_browser(true, picked))
            {
                out_project_file = picked;
                open_now = true;
            }
        }

        if (ImGui::CollapsingHeader("Recent Projects", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (g_recents.empty()) { ImGui::TextDisabled("No recent projects"); }

            int remove_idx = -1;
            for (int i = 0; i < (int)g_recents.size(); ++i)
            {
                const std::string& p = g_recents[i];
                const bool exists = fs::exists(p);

                ImGui::PushID(i);
                if (ImGui::SmallButton("x")) { remove_idx = i; }
                ImGui::SameLine();

                if (!exists) { ImGui::BeginDisabled(); }
                if (ImGui::Selectable(p.c_str()) && exists)
                {
                    out_project_file = p;
                    open_now = true;
                }
                if (!exists) { ImGui::EndDisabled(); }
                ImGui::PopID();
            }

            if (remove_idx >= 0)
            {
                g_recents.erase(g_recents.begin() + remove_idx);
                save_recents();
            }
        }

        if (!status.empty())
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextWrapped("%s", status.c_str());
        }

        ImGui::End();

        return open_now;
    }
} // namespace smol::editor::project_manager
