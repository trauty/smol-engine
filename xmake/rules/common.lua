local engine_dir = path.absolute(path.join(os.scriptdir(), "..", ".."))

local engine_version_checked = false

rule("tau.common")
add_deps("mode.debug", "mode.release", "mode.releasedbg")

on_load(function(target)
    local extra = target:extraconf("rules", "tau.common") or {}

    import("core.project.config")

    if not engine_version_checked then
        engine_version_checked = true

        import("core.base.semver")
        import("core.project.config")
        import("tau_project")

        local provider = config.get("tau_engine_dir")
        local version_file
        for _, cand in ipairs({
            provider and path.join(provider, "share", "tau", "VERSION"),
            provider and path.join(provider, "VERSION"),
            path.join(engine_dir, "VERSION")
        }) do
            if cand and os.isfile(cand) then
                version_file = cand; break
            end
        end

        local proj = tau_project.load(os.projectdir())
        if proj and proj.engine_version and version_file then
            local engine_version = io.readfile(version_file):trim()
            if semver.compare(engine_version, proj.engine_version) < 0 then
                raise("This project needs tau-engine >= " .. proj.engine_version ..
                    ", but the resolved engine is " .. engine_version .. ".\n" ..
                    "Point it at a newer engine: update the tau-engine submodule,\n" ..
                    "vendor it beside the project, or install tau-engine " .. proj.engine_version .. "+.")
            end
        end
    end

    target:set("languages", "cxx20")

    local user_toolchain = config.get("toolchain")

    if target:is_plat("linux") then
        if not user_toolchain then target:set("toolchains", "clang") end
        target:add("cxflags", "-fno-rtti", { force = true })
    elseif target:is_plat("windows") then
        if not user_toolchain then target:set("toolchains", "clang-cl") end

        if (user_toolchain or "clang-cl") == "clang-cl" then
            target:set("toolset.ld", "lld-link")
            target:set("toolset.sh", "lld-link")
            target:set("toolset.ar", "llvm-ar")
        end

        target:add("cxflags", "/GR-", { force = true })
        target:set("runtimes", "MD")
    end

    if extra.march ~= false and is_arch("x86_64", "x64") then
        target:add("cxflags", "-march=x86-64-v3")
    end

    if is_mode("debug") then
        target:set("policy", "build.sanitizer.address", true)

        if target:is_plat("windows") then
            target:add("defines", "_DISABLE_STL_ANNOTATION", { public = true })
        end
    end

    if target:is_plat("linux") and target:kind() == "binary" then
        target:add("rpathdirs", "@loader_path", "@loader_path/../lib")
    end
end)

after_build(function(target)
    import("lib.detect.find_tool")

    if not (target:is_plat("windows") and target:kind() == "binary" and is_mode("debug")) then
        return
    end

    local dll_name = "clang_rt.asan_dynamic-x86_64.dll"
    local clang = find_tool("clang-cl")
    if not (clang and clang.program) then
        return
    end

    local llvm_root = path.directory(path.directory(clang.program))
    local files = os.files(path.join(llvm_root, "lib", "clang", "*", "lib", "windows", dll_name))
    if files and #files > 0 then
        os.trycp(files[1], target:targetdir())
    else
        print("Could not find " .. dll_name)
    end
end)
rule_end()
