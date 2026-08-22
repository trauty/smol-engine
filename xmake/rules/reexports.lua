-- windows only

-- the engine tree this rule ships in; the consuming project's dir is somewhere else
-- entirely once a game includes xmake/smol.lua, so never use os.projectdir() here
local SMOL_ENGINE = path.absolute(path.join(os.scriptdir(), "..", ".."))

rule("smol.reexports")

on_load(function(target)
    if not target:is_plat("windows") then
        return
    end
    target:add("shflags", "/DEF:" .. path.join(target:autogendir(), "reexports.def"), { force = true })
end)

before_link(function(target)
    import("lib.detect.find_tool")

    if not target:is_plat("windows") then
        return
    end

    local nm = find_tool("llvm-nm")
    if not nm then
        raise("smol.reexports needs llvm-nm to enumerate bundled entry points")
    end

    local function defined_symbols(files, keep)
        local names = {}
        if #files == 0 then
            return names
        end
        local argv = { "--defined-only", "--extern-only" }
        table.join2(argv, files)
        for line in os.iorunv(nm.program, argv):gmatch("[^\r\n]+") do
            local name = line:match("^%x+%s+T%s+(%S+)$")
            if name and keep(name) then
                table.insert(names, name)
            end
        end
        return names
    end

    local exports = {}

    table.join2(exports, defined_symbols(
        { path.join(SMOL_ENGINE, "lib", "SDL3", "windows", "SDL3.lib") },
        function(name) return name:startswith("SDL_") and not name:endswith("_REAL") end))

    local fmt_objs = {}
    for _, obj in ipairs(target:objectfiles()) do
        local base = path.filename(obj)
        if base == "format.cc.obj" or base == "os.cc.obj" then
            table.insert(fmt_objs, obj)
        end
    end
    table.join2(exports, defined_symbols(fmt_objs,
        function(name) return name:find("fmt@", 1, true) ~= nil end))

    if #exports == 0 then
        raise("smol.reexports found nothing to export, check llvm-nm output format")
    end

    table.sort(exports)

    local deffile = path.join(target:autogendir(), "reexports.def")
    os.mkdir(path.directory(deffile))
    io.writefile(deffile, "EXPORTS\n" .. table.concat(exports, "\n") .. "\n")
    print("smol.reexports: re-exporting %d symbols", #exports)
end)
rule_end()
