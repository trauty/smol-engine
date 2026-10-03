set_project("sandbox")
set_version("0.0.1")

add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

-- The sample lives inside the engine repository, so the engine is always two levels up. A real
-- project finds it the way smol-game does (TAU_ENGINE_DIR, ./tau-engine, ../tau-engine).
includes(path.join(os.scriptdir(), "..", "..", "xmake", "tau.lua"))

target("sandbox")
    add_rules("tau.game", "tau.hotreload")

    add_files("src/**.cpp")
    add_includedirs("src")
target_end()
