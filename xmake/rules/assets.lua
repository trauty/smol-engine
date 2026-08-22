local engine_dir = path.absolute(path.join(os.scriptdir(), "..", ".."))

rule("smol.assets")
on_buildcmd(function(target, batchcmds, opt)
    local cooker        = target:dep("smol-cooker"):targetfile()
    local engine_assets = path.join(engine_dir, "assets")
    local engine_out    = path.join(target:dep("smol-cooker"):targetdir(), "assets", "engine")

    batchcmds:show("cooking engine assets")
    batchcmds:vrunv(cooker, { "-i", engine_assets, "-o", engine_out, "-n", "engine" })

    local depfiles = os.files(path.join(engine_assets, "**"))
    table.insert(depfiles, cooker)
    table.sort(depfiles)

    batchcmds:add_depfiles(depfiles)
    batchcmds:add_depvalues(table.concat(depfiles, ";"))
    batchcmds:set_depcache(target:dependfile("smol-assets"))
end)
rule_end()
