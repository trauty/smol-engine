if os.projectdir() == os.scriptdir() then
    set_project("smol-engine")
    set_version("0.0.1")
    add_rules("mode.debug", "mode.release")

    set_toolchains("clang")
    set_languages("cxx20")
end

option("profiling")
    set_default(false)
    set_showmenu(true)
    set_description("Enable Tracy Profiling")
    add_defines("SMOL_ENABLE_PROFILING", "TRACY_ENABLE")
option_end()

add_requires("vulkansdk")
add_requires("fmt", "cglm", "joltphysics")

if has_config("profiling") then
    add_requires("tracy")
end

target("smol-engine")
    set_kind("shared")

    if is_mode("release") then
        set_policy("build.optimization.lto", true)
    end

    add_defines("SMOL_EXPORT", "CGLM_FORCE_LEFT_HANDED")

    local root = "$(scriptdir)"

    add_files(path.join(root, "src/smol/**.cpp"))
    add_files(path.join(root, "lib/stb/*.cpp"))
    add_files(path.join(root, "lib/tinygltf/tiny_gltf.cpp"))

    add_includedirs(path.join(root, "include"))
    add_includedirs(path.join(root, "src"))

    add_packages("vulkansdk", "cglm", "joltphysics", "fmt")
    if has_config("profiling") then
        add_packages("tracy")
    end

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
        
        add_cxflags("-stdlib=libc++", {tools = "clang"})
        add_ldflags("-stdlib=libc++", "-fuse-ld=lld", "-rtlib=compiler-rt", {tools = "clang"})
    end

    after_build(function (target)
        local dest_dir = target:targetdir()
        local engine_root = target:scriptdir()
        
        os.cp(path.join(engine_root, "assets"), dest_dir)

        local lib_path = path.join(engine_root, "lib")
        if is_plat("windows") then
            os.cp(path.join(lib_path, "slang/windows/*.dll"), dest_dir)
        elseif is_plat("linux") then
            os.cp(path.join(lib_path, "slang/linux/*.so"), dest_dir)
        end
    end)