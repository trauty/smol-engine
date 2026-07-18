local engine_dir = path.absolute(path.join(os.scriptdir(), "..", ".."))

rule("smol.assets")
on_buildcmd(function(target, batchcmds, opt)
    import("smol_project")

    local cooker        = target:dep("smol-cooker"):targetfile()
    local stage         = path.join(target:dep("smol-cooker"):targetdir(), "assets")

    local projectdir    = os.projectdir()
    local proj          = smol_project.load(projectdir)
    local cooked        = proj and proj.cooked_assets_dir or path.join(projectdir, ".smol")

    local engine_assets = path.join(engine_dir, "assets")
    local engine_out    = path.join(cooked, "engine")

    local depfiles      = os.files(path.join(engine_assets, "**"))
    table.insert(depfiles, cooker)

    batchcmds:show("cooking engine assets")
    batchcmds:vrunv(cooker, { "-i", engine_assets, "-o", engine_out })

    local game_out
    if proj and os.isdir(proj.assets_dir) then
        game_out = path.join(cooked, "game")
        batchcmds:show("cooking game assets")
        batchcmds:vrunv(cooker, { "-i", proj.assets_dir, "-I", engine_assets, "-o", game_out })
        table.join2(depfiles, os.files(path.join(proj.assets_dir, "**")))
    end

    batchcmds:mkdir(stage)
    batchcmds:cp(engine_out, stage)
    if game_out then
        batchcmds:cp(game_out, stage)
    end
    batchcmds:cp(path.join(cooked, "guid_map.json"), stage)

    table.sort(depfiles)
    batchcmds:add_depfiles(depfiles)
    batchcmds:add_depvalues(table.concat(depfiles, ";"))
    batchcmds:set_depcache(target:dependfile("smol-assets"))
end)
rule_end()
