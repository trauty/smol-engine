local engine_dir = path.absolute(path.join(os.scriptdir(), "..", ".."))

local engine_version_checked = false

rule("smol.common")
add_deps("mode.debug", "mode.release", "mode.releasedbg")

on_load(function(target)
    local extra = target:extraconf("rules", "smol.common") or {}

    import("core.project.config")

    if not engine_version_checked then
        engine_version_checked = true

        import("core.base.semver")
        import("core.project.config")
        import("smol_project")

        local provider = config.get("smol_engine_dir")
        local version_file
        for _, cand in ipairs({
            provider and path.join(provider, "share", "smol", "VERSION"),
            provider and path.join(provider, "VERSION"),
            path.join(engine_dir, "VERSION")
        }) do
            if cand and os.isfile(cand) then
                version_file = cand; break
            end
        end

        local proj = smol_project.load(os.projectdir())
        if proj and proj.engine_version and version_file then
            local engine_version = io.readfile(version_file):trim()
            if semver.compare(engine_version, proj.engine_version) < 0 then
                raise("This project needs smol-engine >= " .. proj.engine_version ..
                    ", but the resolved engine is " .. engine_version .. ".\n" ..
                    "Point it at a newer engine: update the submodule, set\n" ..
                    "SMOL_ENGINE_DIR, or install smol-engine " .. proj.engine_version .. "+.")
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
        if not target:is_plat("android") then
            target:set("policy", "build.sanitizer.address", true)
        end

        if target:is_plat("windows") then
            target:add("defines", "_DISABLE_STRING_ANNOTATION",
                "_DISABLE_VECTOR_ANNOTATION", { public = true })
        end
    end

    if target:is_plat("linux") and target:kind() == "binary" then
        target:add("rpathdirs", "@loader_path", "@loader_path/../lib")
    end
end)
rule_end()
