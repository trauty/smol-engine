local is_standalone = (os.scriptdir() == os.projectdir())

local game_name = get_config("game_name") or "smol-engine"
local game_lib_name = get_config("game_lib_name") or "smol-game"

if is_standalone then 
    set_project("smol-engine")
    set_version("0.0.1")
    set_languages("cxx20")
    set_policy("build.c++.rtti", false)
    add_rules("mode.debug", "mode.release", "mode.releasedbg")

    if is_plat("linux") then
        set_toolchains("clang")
        add_ldflags("-static-libstdc++", "-static-libgcc")
        add_cxflags("-fno-rtti", {force = true})
    elseif is_plat("windows") then 
        set_toolchains("clang-cl")
        set_runtimes("MT")
        add_cxflags("/GR-", {force = true})
    end

    if is_mode("debug") then 
        set_policy("build.sanitizer.address", true)
    end
end

option("profiling")
    set_default(false)
    set_showmenu(true)
    set_description("Enable Tracy Profiling")
    add_defines("SMOL_ENABLE_PROFILING", "TRACY_ENABLE")
option_end()

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

add_requires("volk", {system = false, configs = {shared = false}})
add_requires("vulkan-headers")
add_requires("libsdl3", {system = false, configs = {shared = false}})
add_requires("joltphysics", {system = false, configs = {shared = false}})
add_requires("cglm", {system = false, configs = {shared = false}})
add_requires("fmt", {system = false, configs = {shared = false}})
add_requires("entt")
add_requires("slang " .. slang_version)

if has_config("profiling") then 
    add_requires("tracy", {configs = {shared = false}})
end

target("smol-interface")
    set_kind("headeronly")
    
    add_includedirs("include", {public = true})
    add_includedirs("src", {public = true})
    
    add_defines("CGLM_FORCE_LEFT_HANDED", {public = true})
    
    add_packages("volk", "libsdl3", "joltphysics", "cglm", "fmt", "entt", {public = true})
target_end()

target("smol-engine")
    set_kind("static")
    add_cxflags("-march=x86-64-v3")

    if is_mode("release") then 
        set_optimize("fastest")
        set_strip("all")
        set_policy("build.optimization.lto", true)
    end

    add_defines("SMOL_EXPORT", "CGLM_FORCE_LEFT_HANDED")

    add_deps("smol-interface")

    add_packages("slang", {public = true})

    if has_config("profiling") then 
        add_packages("tracy", {public = true})
    end

    add_files("src/smol/**.cpp")
    add_files("lib/stb/*.cpp", {warnings = "none"})
    add_files("lib/tinygltf/tiny_gltf.cpp", {warnings = "none"})
    add_files("lib/vma/vk_mem_alloc.cpp", {warnings = "none"})

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

target("smol-bin")
    set_kind("binary")
    set_basename(game_name)
    add_cxflags("-march=x86-64-v3")

    add_defines(format('SMOL_GAME_NAME="%s"', game_name))
    add_defines(format('SMOL_LIB_NAME="%s"', game_lib_name))

    add_deps("smol-engine", {wholearchive = true})

    add_files("src/smol-bin/**.cpp")

    if is_plat("windows") then
        if is_mode("release") then
            add_ldflags("/subsystem:windows", "/entry:mainCRTStartup", {force = true})
        end
        set_policy("windows.export.all_symbols", true)
    elseif is_plat("linux") then 
        add_rpathdirs("@loader_path")
        add_ldflags("-rdynamic", {force = true})
    end
target_end()