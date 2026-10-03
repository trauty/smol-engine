rule("tau.hotreload")
after_build(function(target)
    import("core.project.depend")

    local targetfile = target:targetfile()
    local trigger = targetfile .. ".trigger"

    depend.on_changed(function()
        io.writefile(trigger, os.date("%Y-%m-%d %H:%M:%S"))
    end, { files = { targetfile }, dependfile = target:dependfile("tau.hotreload") })
end)
rule_end()
