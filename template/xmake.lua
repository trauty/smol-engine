set_project("${NAME}")
set_version("0.0.1")

add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

local function find_smol_engine()
    local env = os.getenv("SMOL_ENGINE_DIR")
    if env and env ~= "" and os.isdir(env) then
        return env
    end

    for _, cand in ipairs({
        path.join(os.scriptdir(), "smol-engine"),
        path.join(os.scriptdir(), "..", "smol-engine"),
    }) do
        if os.isdir(cand) then
            return path.absolute(cand)
        end
    end

    local home = os.getenv("HOME") or os.getenv("USERPROFILE")
    if home then
        local installed = os.dirs(path.join(home, ".smol", "engines", "*"))
        if #installed > 0 then
            table.sort(installed)
            return installed[#installed]
        end
    end

    print("")
    print("  Could not find smol-engine")
    print("")
    print("  Point at it with SMOL_ENGINE_DIR, vendor it at ./smol-engine, place it")
    print("  beside this project as ../smol-engine, or install it under ~/.smol/engines/ on Linux")
    print("")
    smol_engine_was_not_found()
end

includes(path.join(find_smol_engine(), "xmake", "smol.lua"))

target("${NAME}")
    add_rules("smol.game", "smol.hotreload")

    add_files("src/**.cpp")
    add_includedirs("src")
target_end()
