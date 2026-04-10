local engine_dir = path.join(os.scriptdir(), "../../")

rule("smol.android.apk")
after_build(function(target)
    if not target:is_plat("android") then return end

    import("core.project.project")

    local sdk_dir = os.getenv("ANDROID_SDK_ROOT") or os.getenv("ANDROID_HOME") or "/opt/android-sdk"
    local ndk_dir = get_config("ndk") or "/opt/android-ndk"

    local api_level = "34"
    local build_tools = "34.0.0"

    local platform_jar = path.join(sdk_dir, "platforms", "android-" .. api_level, "android.jar")
    local aapt2 = path.join(sdk_dir, "build-tools", build_tools, "aapt2")
    local d8 = path.join(sdk_dir, "build-tools", build_tools, "d8")
    local zipalign = path.join(sdk_dir, "build-tools", build_tools, "zipalign")
    local apksigner = path.join(sdk_dir, "build-tools", build_tools, "apksigner")

    if not os.isfile(aapt2) then
        print("Error: Could not find Android SDK tools at " .. sdk_dir)
        return
    end

    local proj_dir = os.projectdir()
    local out_dir = target:targetdir()

    local work_dir = path.join(out_dir, "apk_workspace")
    local apk_out = path.join(out_dir, "smol-game.apk")

    local template_dir = path.join(engine_dir, "android-template")
    local assets_dir = path.join(proj_dir, ".smol")
    local keystore_path = path.join(proj_dir, "debug.keystore")

    print("Packaging Android APK...")

    os.rm(work_dir)
    os.mkdir(path.join(work_dir, "assets"))
    os.mkdir(path.join(work_dir, "lib/arm64-v8a"))
    os.mkdir(path.join(work_dir, "obj"))
    os.mkdir(path.join(work_dir, "dex"))

    os.cp(path.join(template_dir, "*"), work_dir)

    local game_name = get_config("game_name") or "Smol Game"
    local strings_xml = path.join(work_dir, "res/values/strings.xml")
    if os.isfile(strings_xml) then
        local content = io.readfile(strings_xml)
        content = content:gsub("{GAME_NAME}", game_name)
        io.writefile(strings_xml, content)
        print("Injected game name: '" .. game_name .. "'")
    end

    os.cp(target:targetfile(), path.join(work_dir, "lib/arm64-v8a/"))

    local stl_path = path.join(ndk_dir,
        "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so")
    os.cp(stl_path, path.join(work_dir, "lib/arm64-v8a/"))

    local sdl_dir = target:pkg("libsdl3"):installdir()
    os.cp(path.join(sdl_dir, "lib/libSDL3.so"), path.join(work_dir, "lib/arm64-v8a/"))

    if is_mode("debug") then
        local validation_layer = path.join(ndk_dir,
            "sources/third_party/vulkan/arm64-v8a/libVkLayer_khronos_validation.so")

        if os.isfile(validation_layer) then
            os.cp(validation_layer, path.join(work_dir, "lib/arm64-v8a/"))
            print("Injected Vulkan Validation Layer into APK (Debug Mode)")
        else
            print("Warning: Could not find Vulkan validation layer lib at: " .. validation_layer)
        end
    end

    if os.isdir(assets_dir) then
        os.cp(path.join(assets_dir, "*"), path.join(work_dir, "assets/"))
    else
        print("Warning: Assets directory not found (" .. assets_dir .. "). APK will have no assets.")
    end

    os.execv(aapt2, { "compile", "--dir", path.join(work_dir, "res"), "-o", path.join(work_dir, "res.zip") })

    os.execv(aapt2, {
        "link", "-o", path.join(work_dir, "app-unaligned.apk"),
        "-I", platform_jar,
        "--manifest", path.join(work_dir, "AndroidManifest.xml"),
        "--java", path.join(work_dir, "java"),
        "-A", path.join(work_dir, "assets"),
        path.join(work_dir, "res.zip")
    })

    local java_files = os.files(path.join(work_dir, "java/**.java"))
    local javac_path = "/usr/lib/jvm/java-17-openjdk/bin/javac"
    os.execv(javac_path,
        table.join(
            { "-source", "1.8", "-target", "1.8", "-bootclasspath", platform_jar, "-d", path.join(work_dir, "obj") },
            java_files))

    local class_files = os.files(path.join(work_dir, "obj/**.class"))
    os.execv(d8, table.join({ "--release", "--lib", platform_jar, "--output", path.join(work_dir, "dex") }, class_files))

    local old_dir = os.cd(work_dir)
    os.cp("dex/classes.dex", "classes.dex")
    os.exec("zip -q -u app-unaligned.apk classes.dex")
    os.exec("zip -q -u -0 -r app-unaligned.apk lib/")
    os.cd(old_dir)

    os.execv(zipalign, { "-f", "-p", "4", path.join(work_dir, "app-unaligned.apk"), apk_out })
    os.execv(apksigner, { "sign", "--ks", keystore_path, "--ks-pass", "pass:android", apk_out })

    print("APK built successfully: " .. apk_out)
end)
