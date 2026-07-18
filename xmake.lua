local engine_is_root = (os.scriptdir() == os.projectdir())

local game_name = get_config("game_name") or "smol-engine"

includes("xmake/rules/*.lua")
includes("xmake/tasks/*.lua")
add_moduledirs("xmake/modules")

if engine_is_root then
    set_project("smol-engine")
    set_version("0.1.0")
end

set_languages("cxx20")
add_rules("mode.debug", "mode.release", "mode.releasedbg")


option("profiling")
    set_default(false)
    set_showmenu(true)
    set_description("Enable Tracy Profiling")
    add_defines("SMOL_ENABLE_PROFILING")
    add_defines("TRACY_ENABLE")
option_end()

option("standalone")
    set_default(false)
    set_showmenu(true)
    set_description("Build static standalone game (no editor)")
option_end()

local standalone = has_config("standalone") or is_plat("android")

local VENDOR = os.scriptdir()

local function vendor_platdir()
    if is_plat("android") then
        return "android"
    elseif is_plat("windows") then
        return "windows"
    else
        return "linux"
    end
end

target("smol-interface")
    set_kind("headeronly")

    add_includedirs("include", {public = true})
    add_includedirs("src", {public = true})

    add_defines("CGLM_FORCE_LEFT_HANDED", "VK_NO_PROTOTYPES", "CGLM_FORCE_DEPTH_ZERO_TO_ONE", {public = true})

    add_includedirs("lib/JoltPhysics", {public = true})
    add_defines("JPH_OBJECT_LAYER_BITS=16", "JPH_PROFILE_ENABLED",
                "JPH_DEBUG_RENDERER", "JPH_OBJECT_STREAM", {public = true})
    if not is_mode("debug") then
        add_defines("JPH_NO_DEBUG", {public = true})
    end
target_end()

target("smol-engine")
    if standalone then
        set_kind("static")
        set_optimize("fastest")
        set_strip("all")
        if not is_plat("android") then
            set_policy("build.optimization.lto", true)
        end
        add_defines("SMOL_STATIC_LINK", {public = true})

        if is_plat("android") then
            add_cxflags("-fPIC", {force = true})
        end

        on_load(function (target)
            os.tryrm(path.join(target:targetdir(), "libsmol-engine.so"))
            os.tryrm(path.join(target:targetdir(), "smol-engine.dll"))
        end)
    else
        set_kind("shared")
        if is_plat("linux") then
            add_shflags("-Wl,-Bsymbolic")
            add_rpathdirs("$ORIGIN")
        end
    end

    add_rules("smol.common")

    add_options("profiling", {public = true})

    add_defines("SMOL_ENGINE_EXPORT", "CGLM_FORCE_LEFT_HANDED")

    add_deps("smol-interface")

    add_undefines("JPH_FLOATING_POINT_EXCEPTIONS_ENABLED")

    if has_config("profiling") then
        add_includedirs("lib/tracy", {public = true})   -- <tracy/...>, <common/...>
        add_files("lib/tracy/TracyClient.cpp", {warnings = "none"})
    end

    if not is_plat("windows") then
        add_syslinks("dl")
    end

    add_files("src/smol/**.cpp")
    add_files("lib/vma/vk_mem_alloc.cpp", {warnings = "none"})
    add_files("lib/imgui/**.cpp", {warnings = "none"})

    add_files("lib/volk/volk.c", {warnings = "none"})            -- Vulkan meta-loader
    add_files("lib/fmt/format.cc", "lib/fmt/os.cc", {warnings = "none"})
    add_files("lib/JoltPhysics/Jolt/**.cpp", {warnings = "none"}) -- physics (source, ABI-safe)

    add_linkdirs(path.join("lib", "SDL3", vendor_platdir()), {public = true})
    add_linkdirs(path.join("lib", "ktx", vendor_platdir()), {public = true})
    add_links("SDL3", "ktx", {public = true})
    if is_plat("linux") then
        add_syslinks("pthread", "m", {public = true})   -- SDL3 static needs these
    end

    add_includedirs("include", {public = true})
    add_includedirs("include/imgui", {public = true})
    add_includedirs("src", {public = true})

    add_headerfiles("src/(smol/**.h)")
    add_headerfiles("include/(**)")

    add_headerfiles("lib/JoltPhysics/(Jolt/**.h)")
    add_headerfiles("lib/JoltPhysics/(Jolt/**.inl)")

    add_installfiles("xmake/rules/*.lua", {prefixdir = "share/smol/rules"})
    add_installfiles("xmake/modules/*.lua", {prefixdir = "share/smol/modules"})
    add_installfiles("xmake/tasks/*.lua", {prefixdir = "share/smol/tasks"})
    add_installfiles("template/(**)", {prefixdir = "share/smol/template"})
    add_installfiles("VERSION", {prefixdir = "share/smol"})

    if is_plat("linux") then
        add_installfiles("scripts/smol", {prefixdir = "bin"})
    end

    add_installfiles("assets/(**)", {prefixdir = "share/smol/engine-assets-src"})

    after_install(function (target)
        import("smol_project")

        if target:is_plat("linux") then
            local launcher = path.join(target:installdir(), "bin", "smol")
            if os.isfile(launcher) then os.vrunv("chmod", {"+x", launcher}) end
        end

        local proj = smol_project.load(os.projectdir())
        local cooked = proj and proj.cooked_assets_dir or path.join(os.projectdir(), ".smol")
        local dest = path.join(target:installdir(), "share", "smol", "engine-assets")

        if os.isdir(path.join(cooked, "engine")) then
            os.mkdir(dest)
            os.cp(path.join(cooked, "engine"), dest)
            os.trycp(path.join(cooked, "guid_map.json"), dest)
            os.tryrm(path.join(dest, "engine", "cooker_cache.json"))
        end
    end)
target_end()

target("smol-runtime")
    if is_plat("android") then
        set_kind("shared")

        add_rules("smol.android.apk")
        set_basename("main")
    elseif standalone then
        set_kind("binary")
        set_basename(game_name)
    else
        set_kind("binary")
        set_basename("smol-runtime")
    end

    add_rules("smol.common")

    add_deps("smol-engine")

    on_load(function (target)
        import("core.base.option")
        import("core.project.config")
        import("smol_project")

        if target:is_plat("android") and not option.get("ndk") then
            local home = os.getenv("HOME") or os.getenv("USERPROFILE")
            local ndks = home and os.dirs(path.join(home, ".smol", "android", "sdk", "ndk", "*"))
            if ndks and #ndks > 0 then
                table.sort(ndks)
                config.set("ndk", ndks[#ndks], {force = true})
            end
        end

        if not (config.get("standalone") or target:is_plat("android")) then return end

        local proj = smol_project.load(os.projectdir())
        if proj and proj.startup_scene and proj.startup_scene ~= "" then
            target:add("defines", "SMOL_STARTUP_SCENE=\"" .. proj.startup_scene .. "\"")
        end
    end)

    if not is_plat("android") then
        add_deps("smol-assets", {inherit = false})
    end

    if standalone and not engine_is_root then
        add_deps(game_name)
    end

    add_files("src/smol-runtime/main.cpp")
    if standalone and engine_is_root then
        add_files("src/smol-runtime/empty_game.cpp")
    end

    if is_plat("windows") then
        if is_mode("release") then
            add_ldflags("/subsystem:windows", "/entry:mainCRTStartup", {force = true})
        end
    elseif is_plat("linux") then
        add_ldflags("-rdynamic", {force = true})
    end

    if is_plat("windows") and is_mode("debug") then
        after_build(function (target)
            import("lib.detect.find_tool")
        
            local dest_dir = target:targetdir()
            local dll_name = "clang_rt.asan_dynamic-x86_64.dll"
            local asan_dll = nil
        
            local clang = find_tool("clang-cl")
            if clang and clang.program then
                local llvm_root = path.directory(path.directory(clang.program))
            
                local search_pattern = path.join(llvm_root, "lib", "clang", "*", "lib", "windows", dll_name)
                local files = os.files(search_pattern)
            
                if files and #files > 0 then
                    asan_dll = files[1]
                end
            end

            if asan_dll then
                os.trycp(asan_dll, dest_dir)
                print("Successfully copied Asan from: " .. asan_dll)
            else
                print("Could not find " .. dll_name)
            end
        end)
    end

target_end()

if not is_plat("android") then
target("smol-cooker")
    set_kind("binary")

    add_rules("smol.common", {march = false})

    add_deps("smol-engine")

    add_files("src/smol-cooker/**.cpp")
    add_files("lib/tinygltf/tiny_gltf.cpp", {warnings = "none"})
    add_files("lib/stb/*.cpp", {warnings = "none"})

    add_files("lib/meshoptimizer/*.cpp", {warnings = "none"})

    add_includedirs("include", {public = true})
    add_includedirs("include/slang", {public = true})   -- <slang.h>, <slang-com-ptr.h>
    add_includedirs("src", {public = true})

    local slang_libdir = path.join(VENDOR, "lib", "slang", vendor_platdir())
    add_linkdirs(slang_libdir)
    if is_plat("windows") then
        add_links("slang-compiler")
    else
        local so = os.files(path.join(slang_libdir, "libslang-compiler.so*"))[1]
        if so then add_links(":" .. path.filename(so)) end
    end

    add_linkdirs(path.join(VENDOR, "lib", "spirv-tools", vendor_platdir()))
    add_links("SPIRV-Tools-opt", "SPIRV-Tools")

    after_build(function (target)
        local dest_dir = target:targetdir()
        if target:is_plat("linux") then
            os.trycp(path.join(slang_libdir, "libslang-compiler.so*"), dest_dir)
        elseif target:is_plat("windows") then
            os.trycp(path.join(slang_libdir, "slang-compiler.dll"), dest_dir)
        end
    end)

    after_install(function (target)
        local bindir = path.join(target:installdir(), "bin")
        if target:is_plat("linux") then
            os.trycp(path.join(slang_libdir, "libslang-compiler.so*"), bindir)
        elseif target:is_plat("windows") then
            os.trycp(path.join(slang_libdir, "slang-compiler.dll"), bindir)
        end
    end)
target_end()

target("smol-assets")
    set_kind("phony")
    add_rules("smol.assets")
    add_deps("smol-cooker", {inherit = false})
target_end()
end

if not standalone then
target("smol-editor")
    set_kind("binary")

    add_rules("smol.common")

    if is_plat("windows") then
        if is_mode("release") then
            add_ldflags("/subsystem:windows", "/entry:mainCRTStartup", {force = true})
        end
    end

    add_deps("smol-engine")

    if not is_plat("android") then
        add_deps("smol-assets", {inherit = false})
    end

    if not engine_is_root then
        add_deps(game_name, {inherit = false})
    end

    add_files("src/smol-editor/**.cpp")
    add_includedirs("src")

    add_files("lib/volk/volk.c", {warnings = "none"})
target_end()
end