task("smol-package")
set_category("plugin")

on_run(function()
    import("core.project.config")
    import("smol_project")

    config.load()

    local plat = config.get("plat") or os.host()
    local arch = config.get("arch") or os.arch()
    local mode = config.get("mode") or "release"

    if not (config.get("standalone") or plat == "android") then
        raise("smol-package needs a standalone build.\n" ..
            "run:  xmake f --standalone=y -m " .. mode .. "   &&   xmake")
    end

    local proj = smol_project.load(os.projectdir())
    local name = (proj and proj.project_name) or "smol-game"
    local game_name = config.get("game_name") or name

    local builddir = path.join(os.projectdir(), "build", plat, arch, mode)
    local exe = path.join(builddir, game_name)
    if plat == "windows" then exe = exe .. ".exe" end
    local assets = path.join(builddir, "assets")

    if not os.isfile(exe) then
        raise("standalone binary not found: " .. exe .. "\nbuild it first:  xmake")
    end
    if not os.isdir(assets) then
        raise("cooked assets not found: " .. assets .. "\nbuild it first:  xmake")
    end

    local outdir = path.join(os.projectdir(), "dist", name)
    os.tryrm(outdir)
    os.mkdir(outdir)
    os.cp(exe, path.join(outdir, path.filename(exe)))
    os.cp(assets, path.join(outdir, "assets"))

    cprint("${bright green}Packaged ${clear}%s", name)
    cprint("  binary: %s", path.join(outdir, path.filename(exe)))
    cprint("  assets: %s/", path.join(outdir, "assets"))
    cprint("  ${dim}ship the whole %s/ folder${clear}", path.relative(outdir, os.projectdir()))
end)

set_menu {
    usage       = "xmake smol-package",
    description = "Bundle the standalone game (binary + cooked assets) into dist/<project>/.",
    options     = {},
}
