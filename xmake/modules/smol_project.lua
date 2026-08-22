import("core.base.json")

function load(projectdir)
    local files = os.files(path.join(projectdir, "*.smolproject"))
    if #files == 0 then
        return nil
    end

    local data = json.loadfile(files[1])
    local paths = data.paths or {}

    local engine = data.engine or {}

    return {
        file                = files[1],
        project_name        = data.project_name or "smol-game",
        game_lib_name       = data.game_lib_name or "smol-game-logic",
        startup_scene       = data.startup_scene or "",
        engine_version      = engine.version or data.engine_version,
        engine_path         = engine.path,
        smolproject_version = data.smolproject_version,
        bin_dir             = path.join(projectdir, paths.bin_dir or "bin"),
        assets_dir          = path.join(projectdir, paths.assets_dir or "assets"),
        cooked_assets_dir   = path.join(projectdir, paths.cooked_assets_dir or ".smol"),
    }
end
