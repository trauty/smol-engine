#include "smol-editor/asset_watch.h"

#include "smol-editor/asset_cook.h"
#include "smol/asset_reload.h"
#include "smol/asset_table.h"
#include "smol/log.h"

#include <chrono>
#include <future>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace smol::editor::asset_watch
{
    namespace
    {
        using clock_t = std::chrono::steady_clock;

        constexpr auto POLL_INTERVAL = std::chrono::milliseconds(250);

        // how long a file must hold still to count as written
        // tools save in stages, and cooking on the first movement cooks half a file
        constexpr auto SETTLE_TIME = std::chrono::milliseconds(250);

        struct stamp_t
        {
            i64 write_time = 0;
            u64_t size = 0;

            bool operator==(const stamp_t& other) const
            {
                return write_time == other.write_time && size == other.size;
            }
        };

        struct pending_t
        {
            stamp_t stamp;
            clock_t::time_point last_moved;
        };

        struct polling_backend_t final : backend_t
        {
            fs::path root;
            std::unordered_map<std::string, stamp_t> seen;
            bool primed = false;

            bool start(const fs::path& watch_root) override
            {
                root = watch_root;
                seen.clear();
                primed = false;

                std::error_code ec;
                return fs::is_directory(root, ec);
            }

            void collect(std::vector<fs::path>& out) override
            {
                std::error_code ec;
                std::unordered_set<std::string> present;

                for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec))
                {
                    if (ec || !it->is_regular_file(ec)) { continue; }

                    const std::string key = it->path().generic_string();
                    present.insert(key);

                    const stamp_t stamp = {it->last_write_time(ec).time_since_epoch().count(),
                                           static_cast<u64_t>(it->file_size(ec))};
                    if (ec) { continue; }

                    stamp_t& known = seen[key];
                    if (known == stamp) { continue; }

                    known = stamp;

                    // the first walk defines unchanged, it is not a set of edits
                    if (primed) { out.push_back(it->path()); }
                }

                for (auto it = seen.begin(); it != seen.end();)
                {
                    if (present.count(it->first) == 0) { it = seen.erase(it); }
                    else
                    {
                        it++;
                    }
                }

                primed = true;
            }
        };

        struct root_t
        {
            fs::path dir;
            std::string vfs_prefix;
            bool is_engine = false;
            std::unique_ptr<backend_t> backend;
        };

        // reloads include a partly failed batch, whatever cooked is newer than what is loaded
        struct cook_outcome_t
        {
            std::vector<std::string> reload; // vfs names
            std::string error;
        };

        struct state_t
        {
            std::vector<root_t> roots;
            bool running = false;

            clock_t::time_point last_poll = clock_t::now();

            std::unordered_map<std::string, pending_t> settling;
            std::unordered_set<std::string> self_written;
            std::string open_scene; // as a vfs name

            std::future<cook_outcome_t> cook;
        };

        state_t g_state;

        std::vector<fs::path> g_candidates;

        bool is_inside(const fs::path& path, const fs::path& dir)
        {
            std::error_code ec;
            const std::string rel = fs::relative(path, dir, ec).generic_string();
            return !ec && !rel.empty() && rel.rfind("..", 0) != 0;
        }

        // the cooker takes disk paths, the registry vfs names: root scheme plus path inside the root
        std::string vfs_name_for(const fs::path& path, const fs::path& root, const std::string& prefix)
        {
            std::error_code ec;
            const std::string rel = fs::relative(path, root, ec).generic_string();
            if (ec || rel.empty() || rel.rfind("..", 0) == 0) { return {}; }

            return prefix + rel;
        }

        const root_t* root_for(const fs::path& path)
        {
            for (const root_t& root : g_state.roots)
            {
                if (is_inside(path, root.dir)) { return &root; }
            }
            return nullptr;
        }

        std::string normalise(const fs::path& path)
        {
            std::error_code ec;
            const fs::path canonical = fs::weakly_canonical(path, ec);
            return (ec ? path : canonical).generic_string();
        }

        bool is_cookable(const fs::path& path)
        {
            return smol::asset_table::by_source_extension(path.generic_string()) != nullptr;
        }

        void take_settled(std::vector<fs::path>& out)
        {
            const clock_t::time_point now = clock_t::now();

            for (auto it = g_state.settling.begin(); it != g_state.settling.end();)
            {
                if (now - it->second.last_moved < SETTLE_TIME)
                {
                    it++;
                    continue;
                }

                const auto own = g_state.self_written.find(it->first);
                if (own != g_state.self_written.end())
                {
                    g_state.self_written.erase(own);
                    it = g_state.settling.erase(it);
                    continue;
                }

                out.emplace_back(it->first);
                it = g_state.settling.erase(it);
            }
        }

        // runs off the main thread: engine edits first, into the editor's own engine tree
        // then the project, given the engine files too so only assets that read them are recooked
        cook_outcome_t cook_batch(std::vector<fs::path> game_files, std::vector<fs::path> engine_files,
                                  fs::path game_root, fs::path engine_root)
        {
            cook_outcome_t outcome;

            auto note_error = [&](const std::string& err)
            {
                if (!outcome.error.empty()) { outcome.error += "; "; }
                outcome.error += err;
            };

            if (!engine_files.empty())
            {
                std::vector<fs::path> cooked;
                std::string err;
                if (!asset_cook::cook_engine_assets(engine_files, err, &cooked)) { note_error(err); }

                for (const fs::path& source : cooked)
                {
                    const std::string name = vfs_name_for(source, engine_root, "engine://assets/");
                    if (!name.empty()) { outcome.reload.push_back(name); }
                }
            }

            std::vector<fs::path> project_batch = std::move(game_files);
            project_batch.insert(project_batch.end(), engine_files.begin(), engine_files.end());

            if (!project_batch.empty())
            {
                std::vector<fs::path> cooked;
                std::string err;
                if (!asset_cook::cook_assets(project_batch, err, &cooked)) { note_error(err); }

                for (const fs::path& source : cooked)
                {
                    const std::string name = vfs_name_for(source, game_root, "game://assets/");
                    if (!name.empty()) { outcome.reload.push_back(name); }
                }
            }

            return outcome;
        }

        void apply_reloads(const std::vector<std::string>& reload)
        {
            for (const std::string& vfs_name : reload)
            {
                // the open scene in memory is the newer copy, reloading would discard unsaved work
                if (!g_state.open_scene.empty() && vfs_name == g_state.open_scene)
                {
                    SMOL_LOG_INFO("ASSET_WATCH", "Recooked the open scene but left it alone: {}", vfs_name);
                    continue;
                }

                if (smol::asset_reload::reload_with_dependents(vfs_name))
                {
                    SMOL_LOG_INFO("ASSET_WATCH", "Reloaded {}", vfs_name);
                }
            }
        }
    } // namespace

    std::unique_ptr<backend_t> make_polling_backend()
    {
        return std::make_unique<polling_backend_t>();
    }

    void start(const fs::path& game_root, const fs::path& engine_root, backend_factory_t make_backend)
    {
        stop();

        auto add_root = [&](const fs::path& dir, const char* prefix, bool is_engine)
        {
            root_t root{dir, prefix, is_engine, make_backend()};
            if (!root.backend->start(dir))
            {
                SMOL_LOG_WARN("ASSET_WATCH", "Not watching '{}': it is not a directory", dir.generic_string());
                return;
            }

            SMOL_LOG_INFO("ASSET_WATCH", "Watching {}", dir.generic_string());
            g_state.roots.push_back(std::move(root));
        };

        add_root(game_root, "game://assets/", false);
        if (!engine_root.empty()) { add_root(engine_root, "engine://assets/", true); }

        g_state.running = !g_state.roots.empty();
        g_state.last_poll = clock_t::now();
    }

    void stop()
    {
        if (g_state.cook.valid()) { g_state.cook.wait(); }

        g_state = {};
    }

    void ignore_own_write(const fs::path& path) { g_state.self_written.insert(normalise(path)); }

    void set_open_scene(const fs::path& path)
    {
        g_state.open_scene.clear();
        if (path.empty()) { return; }

        for (const root_t& root : g_state.roots)
        {
            if (root.is_engine) { continue; }
            g_state.open_scene = vfs_name_for(normalise(path), normalise(root.dir), root.vfs_prefix);
        }
    }

    bool is_cooking() { return g_state.cook.valid(); }

    void tick()
    {
        if (!g_state.running) { return; }

        // a finished cook first, so its reloads land before a new one starts
        if (g_state.cook.valid() &&
            g_state.cook.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            const cook_outcome_t outcome = g_state.cook.get();
            g_state.cook = {};

            if (!outcome.error.empty()) { SMOL_LOG_ERROR("ASSET_WATCH", "Could not reimport: {}", outcome.error); }
            apply_reloads(outcome.reload);
        }

        const clock_t::time_point now = clock_t::now();
        if (now - g_state.last_poll < POLL_INTERVAL) { return; }
        g_state.last_poll = now;

        g_candidates.clear();
        for (root_t& root : g_state.roots) { root.backend->collect(g_candidates); }

        // the backend only nominates, the stat decides, which keeps coalesced or duplicate events harmless
        for (const fs::path& candidate : g_candidates)
        {
            if (!is_cookable(candidate)) { continue; }

            std::error_code ec;
            const stamp_t stamp = {fs::last_write_time(candidate, ec).time_since_epoch().count(),
                                   static_cast<u64_t>(fs::file_size(candidate, ec))};
            if (ec) { continue; }

            pending_t& pending = g_state.settling[normalise(candidate)];
            if (!(pending.stamp == stamp))
            {
                pending.stamp = stamp;
                pending.last_moved = now;
            }
        }

        if (g_state.cook.valid()) { return; } // one cook at a time; the rest keeps settling

        std::vector<fs::path> ready;
        take_settled(ready);
        if (ready.empty()) { return; }

        std::vector<fs::path> game_files;
        std::vector<fs::path> engine_files;
        fs::path game_root;
        fs::path engine_root;

        for (const root_t& root : g_state.roots)
        {
            (root.is_engine ? engine_root : game_root) = root.dir;
        }

        for (const fs::path& path : ready)
        {
            const root_t* root = root_for(path);
            if (root == nullptr) { continue; }
            (root->is_engine ? engine_files : game_files).push_back(path);
        }

        SMOL_LOG_INFO("ASSET_WATCH", "{} asset(s) changed on disk, reimporting", ready.size());

        // one process per tree per batch, startup is ~240 ms whatever the cook does
        g_state.cook = std::async(std::launch::async, &cook_batch, std::move(game_files), std::move(engine_files),
                                  std::move(game_root), std::move(engine_root));
    }
} // namespace smol::editor::asset_watch
