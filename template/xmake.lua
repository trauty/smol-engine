set_project("${NAME}")
set_version("0.0.1")

set_config("game_name", "${NAME}")

add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

local function smol_engine_dir()
    local env = os.getenv("SMOL_ENGINE_DIR")
    if env and os.isdir(env) then return env end

    local sub = path.join(os.scriptdir(), "smol-engine")
    if os.isdir(sub) then return sub end

    local sibling = path.join(os.scriptdir(), "..", "smol-engine")
    if os.isdir(sibling) then return sibling end

    local home = os.getenv("HOME") or os.getenv("USERPROFILE")
    if home then
        local installed = os.dirs(path.join(home, ".smol", "engines", "*"))
        if #installed > 0 then
            table.sort(installed)
            return installed[#installed]
        end
    end

    return path.join(os.scriptdir(), "NO_SMOL_ENGINE__set_SMOL_ENGINE_DIR_or_vendor_smol-engine")
end

local SMOL_ENGINE = smol_engine_dir()
set_config("smol_engine_dir", SMOL_ENGINE)
if os.isfile(path.join(SMOL_ENGINE, "xmake.lua")) then
    includes(SMOL_ENGINE)
else
    includes(path.join(SMOL_ENGINE, "share", "smol", "rules", "*.lua"))
    includes(path.join(SMOL_ENGINE, "share", "smol", "tasks", "*.lua"))
    add_moduledirs(path.join(SMOL_ENGINE, "share", "smol", "modules"))
end

target("${NAME}")
    add_rules("smol.game", "smol.hotreload")

    add_files("src/**.cpp")
    add_includedirs("src")
target_end()
