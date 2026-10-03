local TAU_ENGINE = path.normalize(path.absolute(path.join(os.scriptdir(), "..")))

set_config("tau_engine_dir", TAU_ENGINE)

includes(path.join(os.scriptdir(), "rules", "*.lua"))
includes(path.join(os.scriptdir(), "tasks", "*.lua"))
add_moduledirs(path.join(os.scriptdir(), "modules"))

option("standalone")
set_default(false)
set_showmenu(true)
set_description("Build a static standalone game binary (no editor)")
option_end()
