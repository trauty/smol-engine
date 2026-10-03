set_project("${NAME}")
set_version("0.0.1")

add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

local function find_tau_engine()
    local env = os.getenv("TAU_ENGINE_DIR")
    if env and env ~= "" and os.isdir(env) then
        return env
    end

    for _, cand in ipairs({
        path.join(os.scriptdir(), "tau-engine"),
        path.join(os.scriptdir(), "..", "tau-engine"),
    }) do
        if os.isdir(cand) then
            return path.absolute(cand)
        end
    end

    local home = os.getenv("HOME") or os.getenv("USERPROFILE")
    if home then
        local installed = os.dirs(path.join(home, ".tau", "engines", "*"))
        if #installed > 0 then
            table.sort(installed)
            return installed[#installed]
        end
    end

    print("")
    print("  Could not find tau-engine")
    print("")
    print("  Point at it with TAU_ENGINE_DIR, vendor it at ./tau-engine, place it")
    print("  beside this project as ../tau-engine, or install it under ~/.tau/engines/ on Linux")
    print("")
    tau_engine_was_not_found()
end

includes(path.join(find_tau_engine(), "xmake", "tau.lua"))

target("${NAME}")
    add_rules("tau.game", "tau.hotreload")

    add_files("src/**.cpp")
    add_includedirs("src")
target_end()
