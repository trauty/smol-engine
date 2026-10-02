rule("smol.game")
add_deps("smol.common")

on_load(function(target)
    import("core.project.config")
    import("smol_engine")
    import("smol_project")

    local standalone = config.get("standalone")
    local info = smol_engine.resolve(config.get("smol_engine_dir"))
    local proj = smol_project.load(os.projectdir())

    target:add("defines", "SMOL_GAME_EXPORT")
    target:add("files", info.volk_source)

    if standalone then
        target:add("files", info.runtime_main)
        target:add("defines", "SMOL_STATIC_LINK")

        local plat = config.get("plat") or os.host()
        local arch = config.get("arch") or os.arch()
        local mode = config.get("mode") or "release"
        target:set("targetdir", path.join(os.projectdir(), "build", plat, arch, mode))

        target:set("kind", "binary")

        if proj and proj.startup_scene and proj.startup_scene ~= "" then
            target:add("defines", "SMOL_STARTUP_SCENE=\"" .. proj.startup_scene .. "\"")
        end

        local bindir = path.join(os.projectdir(), "bin")
        os.tryrm(path.join(bindir, "lib" .. target:basename() .. ".so"))
        os.tryrm(path.join(bindir, target:basename() .. ".dll"))
    else
        target:set("kind", "shared")
        target:set("targetdir", path.join(os.projectdir(), "bin"))
        if target:is_plat("linux") then
            target:add("shflags", "-Wl,-Bsymbolic")
        end
    end

    if config.get("mode") == "release" then
        target:set("optimize", "fastest")
        target:set("strip", "all")
    end

    for _, dir in ipairs(info.includedirs) do
        target:add("includedirs", dir)
    end
    target:add("defines", "CGLM_FORCE_LEFT_HANDED", "VK_NO_PROTOTYPES", "CGLM_FORCE_DEPTH_ZERO_TO_ONE")
    target:add("defines", "JPH_OBJECT_LAYER_BITS=16", "JPH_PROFILE_ENABLED",
        "JPH_DEBUG_RENDERER", "JPH_OBJECT_STREAM")
    if not is_mode("debug") then
        target:add("defines", "JPH_NO_DEBUG")
    end
    if target:is_plat("windows") then
        target:add("defines", "KHRONOS_STATIC")
    end

    smol_engine.apply_links(target, info, standalone)
end)

before_link(function(target)
    import("core.project.config")
    import("smol_engine")

    local static = config.get("standalone") and true or false
    local info = smol_engine.resolve(config.get("smol_engine_dir"))
    local libfile = smol_engine.library_file(info, target, static)

    if not os.isfile(libfile) then
        smol_engine.missing_artifact_error(info, path.filename(libfile) .. " (looked in " .. info.libdir .. ")",
            static and "static" or "shared")
    end
end)


after_build(function(target)
    import("core.project.config")
    import("core.project.depend")
    import("smol_engine")
    import("smol_project")

    local info = smol_engine.resolve(config.get("smol_engine_dir"))
    local proj = smol_project.load(os.projectdir())
    if not proj or not proj.assets_dir or not os.isdir(proj.assets_dir) then
        return
    end

    if not os.isfile(info.cooker) then
        smol_engine.missing_artifact_error(info, "smol-cooker (needed to cook assets)", "cooker")
    end

    local function force_if_missing(dependfile, ...)
        for _, out in ipairs({ ... }) do
            if not os.exists(out) then
                os.tryrm(dependfile)
                return
            end
        end
    end

    local engine_out = path.join(proj.cooked_assets_dir, "engine")
    local game_out = path.join(proj.cooked_assets_dir, "game")
    local guid_map = path.join(proj.cooked_assets_dir, "guid_map.json")

    if not os.isdir(info.engine_cooked) then
        smol_engine.missing_artifact_error(info, "cooked engine assets at " .. info.engine_cooked, "assets")
    end

    local engine_depfile = target:dependfile("smol.game.engine_assets")
    force_if_missing(engine_depfile, engine_out, guid_map)

    depend.on_changed(function()
        import("core.base.json")

        os.tryrm(engine_out)
        os.mkdir(proj.cooked_assets_dir)
        os.cp(info.engine_cooked, proj.cooked_assets_dir)
        os.tryrm(path.join(engine_out, "cooker_cache.json"))

        local merged = {}
        for _, mapfile in ipairs({ guid_map, info.engine_guid_map }) do
            if os.isfile(mapfile) then
                for key, value in pairs(json.loadfile(mapfile)) do
                    merged[key] = value
                end
            end
        end
        json.savefile(guid_map, merged)
    end, {
        files = os.files(path.join(info.engine_cooked, "**")),
        dependfile = engine_depfile
    })

    local depfiles = os.files(path.join(proj.assets_dir, "**"))
    table.insert(depfiles, info.cooker)

    local game_depfile = target:dependfile("smol.game.assets")
    force_if_missing(game_depfile, game_out)

    depend.on_changed(function()
        os.vrunv(info.cooker, { "-i", proj.assets_dir, "-I", info.engine_assets, "-I", info.shader_include,
                                "-o", game_out, "-n", "game" })
    end, { files = depfiles, dependfile = game_depfile })

    if config.get("standalone") then
        local stage = path.join(target:targetdir(), "assets")

        os.tryrm(stage)
        os.mkdir(stage)
        os.cp(engine_out, stage)
        os.cp(game_out, stage)
        os.cp(guid_map, stage)

        for _, junk in ipairs(os.files(path.join(stage, "**", "cooker_cache.json"))) do
            os.rm(junk)
        end
    end
end)
rule_end()
