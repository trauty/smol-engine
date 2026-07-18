rule("smol.game")
add_deps("smol.common")

on_load(function(target)
    import("core.project.config")
    if config.get("standalone") or target:is_plat("android") then
        target:set("kind", "static")
        if target:is_plat("android") then
            target:add("cxflags", "-fPIC", { force = true })
        end

        local bindir = path.join(os.projectdir(), "bin")
        os.tryrm(path.join(bindir, "lib" .. target:basename() .. ".so"))
        os.tryrm(path.join(bindir, target:basename() .. ".dll"))
    else
        target:set("kind", "shared")
        if target:is_plat("linux") then
            target:add("shflags", "-Wl,-Bsymbolic")
        end
    end

    if config.get("mode") == "release" then
        target:set("optimize", "fastest")
        target:set("strip", "all")
    end

    target:add("defines", "SMOL_GAME_EXPORT")

    target:set("targetdir", path.join(os.projectdir(), "bin"))

    local provider = config.get("smol_engine_dir")
    local source_mode = (not provider) or os.isfile(path.join(provider, "xmake.lua"))

    if source_mode then
        target:add("deps", "smol-interface", "smol-engine")
    else
        target:add("includedirs", path.join(provider, "include"))
        target:add("includedirs", path.join(provider, "include", "imgui"))
        target:add("defines", "CGLM_FORCE_LEFT_HANDED", "VK_NO_PROTOTYPES",
            "CGLM_FORCE_DEPTH_ZERO_TO_ONE")

        target:add("defines", "JPH_OBJECT_LAYER_BITS=16", "JPH_PROFILE_ENABLED",
            "JPH_DEBUG_RENDERER", "JPH_OBJECT_STREAM")
        if not is_mode("debug") then
            target:add("defines", "JPH_NO_DEBUG")
        end
        target:add("linkdirs", path.join(provider, "lib"))
        target:add("links", "smol-engine")

        target:add("rpathdirs", path.join(provider, "lib"))
    end
end)

after_build(function(target)
    import("core.project.config")
    import("core.project.depend")
    import("smol_project")

    local provider = config.get("smol_engine_dir")
    local source_mode = (not provider) or os.isfile(path.join(provider, "xmake.lua"))
    if source_mode then return end

    local cooker = path.join(provider, "bin", "smol-cooker")
    if target:is_plat("windows") then cooker = cooker .. ".exe" end
    if not os.isfile(cooker) then
        raise("Binary-provider cook needs the SDK cooker at " .. cooker ..
            ", but it is missing. Reinstall the engine SDK.")
    end

    local proj = smol_project.load(os.projectdir())
    if not proj or not proj.assets_dir or not os.isdir(proj.assets_dir) then
        return
    end

    local engine_src = path.join(provider, "share", "smol", "engine-assets-src")
    local game_out = path.join(proj.cooked_assets_dir, "game")

    local depfiles = os.files(path.join(proj.assets_dir, "**"))
    table.insert(depfiles, cooker)

    depend.on_changed(function()
        os.vrunv(cooker, { "-i", proj.assets_dir, "-I", engine_src, "-o", game_out })
    end, { files = depfiles, dependfile = target:dependfile("smol.game.assets") })
end)
rule_end()
