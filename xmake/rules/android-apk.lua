local engine_dir     = path.join(os.scriptdir(), "../../")

local ANDROID_ABI    = "arm64-v8a"
local ANDROID_TRIPLE = "aarch64-linux-android"

local function ndk_host_tag()
    local h = os.host()
    if h == "macosx" then return "darwin-x86_64" end
    if h == "windows" then return "windows-x86_64" end
    return "linux-x86_64"
end

rule("smol.android.apk")
after_build(function(target)
    if not target:is_plat("android") then return end

    import("lib.detect.find_tool")
    import("smol_project")

    local abi = target:arch()
    if abi ~= ANDROID_ABI then
        raise("smol.android.apk: smol only supports the " .. ANDROID_ABI .. " Android ABI (got '" ..
            tostring(abi) .. "'); configure with  xmake f -p android -a " .. ANDROID_ABI)
    end
    local triple = ANDROID_TRIPLE

    local home = os.getenv("HOME") or os.getenv("USERPROFILE")
    local smol_android = home and path.join(home, ".smol", "android")
    local setup_hint = "run:  xmake smol-android-setup   " ..
        "(downloads the SDK + NDK + JDK to ~/.smol/android)"

    local sdk_dir
    if smol_android and os.isdir(path.join(smol_android, "sdk")) then sdk_dir = path.join(smol_android, "sdk") end
    sdk_dir = sdk_dir or os.getenv("ANDROID_SDK_ROOT") or os.getenv("ANDROID_HOME")

    local ndk_dir = get_config("ndk")
    if not ndk_dir and sdk_dir then
        local ndks = os.dirs(path.join(sdk_dir, "ndk", "*"))
        if ndks and #ndks > 0 then
            table.sort(ndks); ndk_dir = ndks[#ndks]
        end
    end
    ndk_dir = ndk_dir or os.getenv("ANDROID_NDK_ROOT") or os.getenv("ANDROID_NDK_HOME")
    if not ndk_dir or not os.isdir(ndk_dir) then
        raise("smol.android.apk: Android NDK not found (set --ndk or ANDROID_NDK_ROOT).\n" .. setup_hint)
    end

    local api_level = "34"
    local build_tools = "34.0.0"

    local platform_jar = path.join(sdk_dir, "platforms", "android-" .. api_level, "android.jar")
    local aapt2 = path.join(sdk_dir, "build-tools", build_tools, "aapt2")
    local d8 = path.join(sdk_dir, "build-tools", build_tools, "d8")
    local zipalign = path.join(sdk_dir, "build-tools", build_tools, "zipalign")
    local apksigner = path.join(sdk_dir, "build-tools", build_tools, "apksigner")

    if not os.isfile(aapt2) then
        raise("smol.android.apk: Android SDK build-tools " .. build_tools .. " not found" ..
            (sdk_dir and (" at " .. sdk_dir) or "") .. ".\n" .. setup_hint)
    end

    local javac
    local java_home
    if smol_android and os.isdir(path.join(smol_android, "jdk")) then java_home = path.join(smol_android, "jdk") end
    java_home = java_home or os.getenv("JAVA_HOME")
    if java_home then
        local candidate = path.join(java_home, "bin", "javac")
        if os.isfile(candidate) then javac = candidate end
    end
    if not javac then
        local tool = find_tool("javac")
        javac = tool and tool.program
    end
    if not javac then
        raise("smol.android.apk: javac not found (set JAVA_HOME or add javac to PATH).\n" .. setup_hint)
    end

    local proj_dir = os.projectdir()
    local out_dir = target:targetdir()

    local proj = smol_project.load(proj_dir)
    local cooked = proj and proj.cooked_assets_dir or path.join(proj_dir, ".smol")
    local guid_map = path.join(cooked, "guid_map.json")

    local cook_hint = "Cook on the host first:\n" ..
        "  xmake f -p linux -a x86_64\n" ..
        "  xmake build smol-assets\n" ..
        "then reconfigure for Android."

    if not os.isdir(cooked) or not os.isfile(guid_map) then
        raise("smol.android.apk: cooked assets not found at " .. cooked .. "\n" .. cook_hint)
    end

    local cooked_mtime = os.mtime(guid_map)
    local sources = os.files(path.join(engine_dir, "assets", "**"))
    if proj then
        table.join2(sources, os.files(path.join(proj.assets_dir, "**")))
    end
    for _, f in ipairs(sources) do
        if os.mtime(f) > cooked_mtime then
            raise("smol.android.apk: cooked assets at " .. cooked .. " are stale (" ..
                f .. " is newer than guid_map.json)\n" .. cook_hint)
        end
    end

    local work_dir = path.join(out_dir, "apk_workspace")
    local apk_name = get_config("game_name") or (proj and proj.project_name) or "smol-game"
    apk_name = apk_name:gsub("[^%w%-_]", "_")
    local apk_out = path.join(out_dir, apk_name .. ".apk")

    local template_dir = path.join(engine_dir, "android-template")
    local keystore_path = path.join(proj_dir, "debug.keystore")

    local abi_lib = path.join("lib", abi)

    print("Packaging Android APK (" .. abi .. ")...")

    os.rm(work_dir)
    os.mkdir(path.join(work_dir, "assets"))
    os.mkdir(path.join(work_dir, abi_lib))
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

    local android = (proj and proj.android) or {}
    local package_id = android.package_id or "com.smol.game"
    local manifest = path.join(work_dir, "AndroidManifest.xml")
    if os.isfile(manifest) then
        local content = io.readfile(manifest)
        content = content:gsub("{PACKAGE_ID}", package_id)
        content = content:gsub("{VERSION_CODE}", tostring(android.version_code or 1))
        content = content:gsub("{VERSION_NAME}", tostring(android.version_name or "1.0"))
        content = content:gsub("{MIN_SDK}", tostring(android.min_sdk or 24))
        content = content:gsub("{TARGET_SDK}", tostring(android.target_sdk or api_level))
        io.writefile(manifest, content)
        print("Injected package id: '" .. package_id .. "'")
    end

    if android.icon then
        local icon = android.icon
        if not path.is_absolute(icon) then icon = path.join(proj_dir, icon) end
        if os.isfile(icon) then
            for _, mip in ipairs(os.dirs(path.join(work_dir, "res", "mipmap-*"))) do
                os.cp(icon, path.join(mip, "ic_launcher.png"))
            end
            print("Injected launcher icon: " .. icon)
        else
            print("Warning: android.icon not found at " .. icon .. " (keeping default)")
        end
    end

    os.cp(target:targetfile(), path.join(work_dir, abi_lib))

    local stl_path = path.join(ndk_dir,
        "toolchains/llvm/prebuilt", ndk_host_tag(), "sysroot/usr/lib", triple, "libc++_shared.so")
    os.cp(stl_path, path.join(work_dir, abi_lib))

    local sdl_so = path.join(engine_dir, "lib", "SDL3", "android", "libSDL3.so")
    if not os.isfile(sdl_so) then
        raise("smol.android.apk: vendored Android SDL3 not found at " .. sdl_so ..
            "\n(place a libSDL3.so built for " .. ANDROID_ABI .. " there)")
    end
    os.cp(sdl_so, path.join(work_dir, abi_lib))

    if is_mode("debug") then
        local validation_layer = path.join(ndk_dir,
            "sources/third_party/vulkan", abi, "libVkLayer_khronos_validation.so")

        if os.isfile(validation_layer) then
            os.cp(validation_layer, path.join(work_dir, abi_lib))
            print("Injected Vulkan Validation Layer into APK (Debug Mode)")
        else
            print("Warning: Could not find Vulkan validation layer lib at: " .. validation_layer)
        end
    end

    os.cp(path.join(cooked, "*"), path.join(work_dir, "assets/"))

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
    os.execv(javac,
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
