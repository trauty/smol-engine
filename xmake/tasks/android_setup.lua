task("smol-android-setup")
set_category("plugin")

on_run(function()
    import("core.base.option")
    import("android_toolchain")

    android_toolchain.provision({ force = option.get("force") })
end)

set_menu {
    usage = "xmake smol-android-setup [--force]",
    description = "Download the Android SDK/NDK/JDK toolchain into ~/.smol/android.",
    options =
    {
        { 'f', "force", "k", nil, "Re-download every component even if already present." }
    }
}
task_end()
