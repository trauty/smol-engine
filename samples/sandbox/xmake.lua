set_project("sandbox")
set_version("0.0.1")

add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

-- The sample lives inside the engine repository, so the engine is always two levels up. A real
-- project finds it the way smol-game does (SMOL_ENGINE_DIR, ./smol-engine, ../smol-engine).
includes(path.join(os.scriptdir(), "..", "..", "xmake", "smol.lua"))

target("sandbox")
    add_rules("smol.game", "smol.hotreload")

    add_files("src/**.cpp")
    add_includedirs("src")
target_end()
