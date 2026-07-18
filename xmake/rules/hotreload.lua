rule("smol.hotreload")
after_build(function(target)
    if target:is_plat("android") then
        return
    end

    import("core.project.depend")

    local targetfile = target:targetfile()
    local trigger = targetfile .. ".trigger"

    depend.on_changed(function()
        io.writefile(trigger, os.date("%Y-%m-%d %H:%M:%S"))
    end, { files = { targetfile }, dependfile = target:dependfile("smol.hotreload") })
end)
rule_end()
