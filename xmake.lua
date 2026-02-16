if os.scriptdir() == os.projectdir() then
    set_project("smol-engine")
    set_version("0.0.1")
    
    set_languages("cxx20")
    if is_mode("debug") then
        set_policy("build.sanitizer.address", true)
    end 

    add_rules("mode.debug", "mode.release")
end

option("profiling")
    set_default(false)
    set_showmenu(true)
    set_description("Enable Tracy Profiling")
    add_defines("SMOL_ENABLE_PROFILING", "TRACY_ENABLE")
option_end()


add_requires("vulkansdk")

target("smol-engine")
    set_kind("shared")
    set_languages("cxx20")
    
    add_cxflags(
        "-mavx",
        "-mavx2",
        "-mfma",
        "-mbmi",
        "-mbmi2",
        "-mf16c",
        "-mlzcnt",
        "-mpopcnt"
    )

    if is_mode("release") then
        set_optimize("fastest")
        set_strip("all")
        set_policy("build.optimization.lto", true)
    end

    local pkg_root = path.join(os.scriptdir(), ".pkgcache")

    on_load(function (target)
        if not os.isdir(pkg_root) then os.mkdir(pkg_root) end

        local function git_clone(name, url, tag)
            local dir = path.join(pkg_root, name)
            if not os.isdir(dir) then
                print("Downloading " .. name .. "...")
                os.execv("git", {"clone", "--depth", "1", "-b", tag, url, dir})
            end
        end

        git_clone("jolt",  "https://github.com/jrouwe/JoltPhysics.git", "v5.5.0")
        git_clone("cglm",  "https://github.com/recp/cglm.git",          "v0.9.6")
        git_clone("fmt",   "https://github.com/fmtlib/fmt.git",         "12.1.0")
        git_clone("tracy", "https://github.com/wolfpld/tracy.git",      "v0.13.1")
    end)

    local root = os.scriptdir()
    local pkg_rel = ".pkgcache"

    add_defines("SMOL_EXPORT", "CGLM_FORCE_LEFT_HANDED")

    add_files(path.join(root, "src/smol/**.cpp"))
    add_files(path.join(root, "lib/stb/*.cpp"), {warnings = "none"})
    add_files(path.join(root, "lib/tinygltf/tiny_gltf.cpp"), {warnings = "none"})

    add_files(path.join(root, pkg_rel, "jolt/Jolt/**.cpp"), {warnings = "none"})
    add_files(path.join(root, pkg_rel, "fmt/src/format.cc"), {warnings = "none"})
    add_files(path.join(root, pkg_rel, "fmt/src/os.cc"), {warnings = "none"})

    add_defines("JPH_CROSS_PLATFORM_DETERMINISTIC")
    add_defines("JPH_FLOATING_POINT_EXCEPTIONS_ENABLED")
    
    if has_config("profiling") then 
        add_defines("JPH_PROFILE_ENABLED")
        add_files(path.join(root, pkg_rel, "tracy/public/common/*.cpp"), {warnings = "none"})
        add_files(path.join(root, pkg_rel, "tracy/public/TracyClient.cpp"), {warnings = "none"})
        if is_plat("windows") then 
            add_syslinks("dbghelp", "ws2_32")
        end
    else 
        add_defines("JPH_PROFILE_ENABLED=0")
    end 

    add_includedirs(path.join(root, "include"), {public = true})
    add_includedirs(path.join(root, "src"), {public = true})
    add_includedirs(path.join(root, pkg_rel, "tracy/public"), {public = true})
    add_includedirs(path.join(root, pkg_rel, "jolt"), {public = true})
    add_includedirs(path.join(root, pkg_rel, "cglm/include"), {public = true})
    add_includedirs(path.join(root, pkg_rel, "fmt/include"), {public = true})

    add_packages("vulkansdk", {public = true})

    local lib_dir = path.join(root, "lib")

    add_includedirs(path.join(root, "include/SDL3"))
    add_includedirs(path.join(root, "include/slang"))

    if is_plat("windows") then
        add_linkdirs(path.join(lib_dir, "SDL3/windows"))
        add_linkdirs(path.join(lib_dir, "slang/windows"))
        add_links("SDL3", "slang")
        add_syslinks("imm32", "version", "setupapi", "winmm", "user32")

    elseif is_plat("linux") then
        add_linkdirs(path.join(lib_dir, "SDL3/linux"))
        add_linkdirs(path.join(lib_dir, "slang/linux"))
        add_links("SDL3", "slang")

        add_shflags("-Xlinker -soname -Xlinker libsmol-engine.so", {force = true})
        add_syslinks("dl", "pthread")
    end

    after_build(function (target)
        local dest_dir = target:targetdir()
        local engine_root = target:scriptdir()

        local lib_path = path.join(engine_root, "lib")
        if is_plat("windows") then
            os.trycp(path.join(lib_path, "slang/windows/*.dll"), dest_dir)
            os.trycp(path.join(lib_path, "vulkan/windows/*.dll"), dest_dir)
        elseif is_plat("linux") then
            os.trycp(path.join(lib_path, "slang/linux/*.so"), dest_dir)
            os.trycp(path.join(lib_path, "vulkan/linux/libvulkan.so.1"), dest_dir)
        end
    end)
target_end()