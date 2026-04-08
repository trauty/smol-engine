local is_standalone = (os.scriptdir() == os.projectdir())

local game_name = get_config("game_name") or "smol-engine"
local game_lib_name = get_config("game_lib_name") or "smol-game"

includes("xmake/rules/*.lua")

if is_standalone then 
    set_project("smol-engine")
    set_version("0.0.1")
    set_languages("cxx20")
    set_policy("build.c++.rtti", false)
    add_rules("mode.debug", "mode.release", "mode.releasedbg")

    if is_plat("linux") then
        set_toolchains("clang")
        add_cxflags("-fno-rtti", {force = true})
    elseif is_plat("windows") then
        set_toolchains("clang-cl")
        set_toolset("ld", "lld-link") 
        set_toolset("sh", "lld-link")
        set_toolset("ar", "llvm-ar")

        add_cxflags("/GR-", {force = true})
        set_runtimes("MD")
    end
end

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

local is_standalone = has_config("standalone")

if not is_plat("android") then
package("slang")
    set_homepage("https://github.com/shader-slang/slang")
    set_description("Slang Shader Language Compiler")

    add_links("slang-compiler")

    local slang_version = "2026.4.2"

    if is_plat("windows") then
        set_urls("https://github.com/shader-slang/slang/releases/download/v$(version)/slang-$(version)-windows-x86_64.zip")
        add_versions(slang_version, "99ac35f3f3843650a8e912f759ee0e0cd691d560b003022beef2620a35af55d4") 
    elseif is_plat("linux") then
        set_urls("https://github.com/shader-slang/slang/releases/download/v$(version)/slang-$(version)-linux-x86_64.tar.gz")
        add_versions(slang_version, "c5346e743cd96a496e24d990a3f62146dccdcec48f4c3f042c097125c59b3ddd")
    end

    on_install(function (package)
        os.cp("include/*", package:installdir("include"))
        os.cp("bin/*", package:installdir("bin"))
        os.cp("lib/*", package:installdir("lib"))
    end)
package_end()

add_requires("slang " .. slang_version)
end

add_requires("vulkan-headers")
add_requires("libsdl3 3.4.2", {system = false, configs = {shared = is_plat("android")}})

add_requires("joltphysics 5.5.0", 
    {
        system = false, 
        configs = {
            shared = false,
            cxflags = is_arch("x86_64", "x64") and "-march=x86-64-v3" or nil,
            rtti = is_plat("android")
        }
    }
)

add_requires("cglm 0.9.6", {system = false, configs = {shared = false}})
add_requires("fmt 12.1.0", {system = false, configs = {shared = false}})
add_requires("entt 3.16.0", {system = false, configs = {shared = false}})

if has_config("profiling") then 
    add_requires("tracy 0.13.1", {system = false, configs = {shared = false}})
end

add_requires("meshoptimizer 1.0.1", {system = false, configs = {shared = false}})
add_requires("ktx 4.4.2", {system = false, configs = {shared = false}})

target("smol-interface")
    set_kind("headeronly")
    
    add_includedirs("include", {public = true})
    add_includedirs("include/volk", {public = true})
    add_includedirs("src", {public = true})
    
    add_defines("CGLM_FORCE_LEFT_HANDED", {public = true})
    
    add_packages("libsdl3", "cglm", "fmt", "entt", {public = true})
target_end()

target("smol-engine")
    if is_standalone then 
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
    else 
        set_kind("shared")
        if is_plat("linux") then 
            add_shflags("-Wl,-Bsymbolic")
        end 
    end

    if is_arch("x86_64", "x64") then
        add_cxflags("-march=x86-64-v3")
    end

    add_options("profiling", {public = true})

    if is_mode("debug") then
        if not is_plat("android") then
            set_policy("build.sanitizer.address", true)
        end

        if is_plat("windows") then
            add_defines("_DISABLE_STRING_ANNOTATION", "_DISABLE_VECTOR_ANNOTATION", {public = true})
        end
    end

    add_defines("SMOL_EXPORT", "CGLM_FORCE_LEFT_HANDED")

    add_deps("smol-interface")

    add_packages("joltphysics", {public = true})
    add_undefines("JPH_FLOATING_POINT_EXCEPTIONS_ENABLED")
    add_packages("ktx", {public = true})

    if has_config("profiling") then
        add_packages("tracy", {public = true})
    end

    if not is_plat("windows") then
        add_syslinks("dl")
    end

    add_files("src/smol/**.cpp")
    add_files("lib/vma/vk_mem_alloc.cpp", {warnings = "none"})
    add_files("lib/imgui/**.cpp", {warnings = "none"})
    add_files("lib/volk/volk.c", {warnings = "none"})

    add_includedirs("include", {public = true})
    add_includedirs("include/imgui", {public = true})
    add_includedirs("src", {public = true})
target_end()

target("smol-bin")
    if is_plat("android") then
        set_kind("shared")
        add_packages("libsdl3")
        add_rules("smol.android.apk")
        set_basename("main")
    else
        set_kind("binary")
        set_basename(game_name)
    end
    
    if is_arch("x86_64", "x64") then
        add_cxflags("-march=x86-64-v3")
    end

    if is_mode("debug") then
        if not is_plat("android") then
            set_policy("build.sanitizer.address", true)
        end

        if is_plat("windows") then
            add_defines("_DISABLE_STRING_ANNOTATION", "_DISABLE_VECTOR_ANNOTATION", {public = true})
        end
    end

    add_defines(format('SMOL_GAME_NAME="%s"', game_name))

    add_deps("smol-engine")

    if is_standalone then
        add_deps("smol-game") 
    end

    add_files("src/smol-bin/**.cpp")

    if is_plat("windows") then
        if is_mode("release") then
            add_ldflags("/subsystem:windows", "/entry:mainCRTStartup", {force = true})
        end
    elseif is_plat("linux") then 
        add_rpathdirs("@loader_path")
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

    on_config(function (target)
        import("core.project.project")

        local game_target = project.target("smol-game")
        local game_lib_path = path.absolute(game_target:targetfile())

        game_lib_path = game_lib_path:gsub("\\", "/")

        target:add("defines", "SMOL_LIB_PATH=\"" .. game_lib_path .. "\"")
    end)
target_end()

if not is_plat("android") then
target("smol-cooker")
    set_kind("binary")

    if is_plat("linux") then 
        add_rpathdirs("@loader_path")
    end

    if is_mode("debug") then
        set_policy("build.sanitizer.address", true)

        if is_plat("windows") then
            add_defines("_DISABLE_STRING_ANNOTATION", "_DISABLE_VECTOR_ANNOTATION", {public = true})
        end
    end

    add_deps("smol-engine")

    add_packages("slang", {public = true})
    add_packages("meshoptimizer", {public = true})

    add_files("src/smol-cooker/**.cpp")
    add_files("lib/tinygltf/tiny_gltf.cpp", {warnings = "none"})
    add_files("lib/stb/*.cpp", {warnings = "none"})

    add_includedirs("include", {public = true})
    add_includedirs("src", {public = true})

    after_build(function (target)
        local dest_dir = target:targetdir()
        local slang_dir = target:pkg("slang")

        if slang_dir then
            if is_plat("linux") then 
                os.trycp(path.join(slang_dir:installdir(), "lib", "libslang-compiler.so.0.*"), dest_dir)
            elseif is_plat("windows") then 
                os.trycp(path.join(slang_dir:installdir(), "bin", "slang-compiler.dll"), dest_dir)
            end
        end  
    end)
target_end()
end

if not is_standalone then
target("smol-editor")
    set_kind("binary")

    if is_arch("x86_64", "x64") then
        add_cxflags("-march=x86-64-v3")
    end

    if is_plat("linux") then 
        add_rpathdirs("@loader_path")
    end

    add_defines(format('SMOL_GAME_NAME="%s"', game_name))

    if is_mode("debug") then
        set_policy("build.sanitizer.address", true)

        if is_plat("windows") then
            add_defines("_DISABLE_STRING_ANNOTATION", "_DISABLE_VECTOR_ANNOTATION", {public = true})
        end
    end

    if is_plat("windows") then
        if is_mode("release") then
            add_ldflags("/subsystem:windows", "/entry:mainCRTStartup", {force = true})
        end
    end

    add_deps("smol-engine")
    add_deps("smol-game", {inherit = false})

    add_files("src/smol-editor/**.cpp")
    add_includedirs("src")

    on_config(function (target)
        import("core.project.project")

        local game_target = project.target("smol-game")
        local game_lib_path = path.absolute(game_target:targetfile())

        game_lib_path = game_lib_path:gsub("\\", "/")

        target:add("defines", "SMOL_LIB_PATH=\"" .. game_lib_path .. "\"")
    end)
target_end()
end