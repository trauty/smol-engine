-- On-demand Android toolchain provisioning for smol.
--
-- Downloads a JDK + the Android command-line tools and, through sdkmanager, the
-- platform / build-tools / NDK a smol Android build needs, all under
-- ~/.smol/android/. This is the ONLY place that downloads anything -- it runs
-- only from the `smol-android-setup` task, so desktop builds never touch it.
--
-- Everything is idempotent: components already present are left alone, so the
-- task is safe to re-run (e.g. after adding an ABI or bumping a pin).

import("net.http")
import("utils.archive")

local JDK_FEATURE    = "17"
local CMDTOOLS_BUILD = "11076708"
local ANDROID_API    = "34"
local BUILD_TOOLS    = "34.0.0"
local NDK_VERSION    = "27.2.12479018"

local function host_os()
    local h = os.host()
    if h == "macosx" then return "mac" end
    return h
end

local function jdk_arch()
    local a = os.arch()
    if a == "arm64" or a == "aarch64" then return "aarch64" end
    if a == "x86" or a == "i386" then return "x86" end
    return "x64"
end

function _smol_android_dir()
    local home = os.getenv("HOME") or os.getenv("USERPROFILE")
    assert(home, "android_toolchain: cannot resolve home directory (HOME/USERPROFILE)")
    return path.join(home, ".smol", "android")
end

local function find_java_home(root)
    local exe = is_host("windows") and "javac.exe" or "javac"
    local hits = os.files(path.join(root, "**", "bin", exe))
    if #hits == 0 then return nil end
    return path.directory(path.directory(hits[1]))
end

local function provision_jdk(jdk_dir, staging)
    local os_label = host_os()
    local ext = (os_label == "windows") and "zip" or "tar.gz"
    local url = format(
        "https://api.adoptium.net/v3/binary/latest/%s/ga/%s/%s/jdk/hotspot/normal/eclipse",
        JDK_FEATURE, os_label, jdk_arch())

    local archive_file = path.join(staging, "jdk." .. ext)
    local extract_dir = path.join(staging, "jdk")

    cprint("${bright}Downloading Temurin JDK %s (%s/%s)...${clear}", JDK_FEATURE, os_label, jdk_arch())
    http.download(url, archive_file)
    os.tryrm(extract_dir)
    archive.extract(archive_file, extract_dir)

    local java_home = find_java_home(extract_dir)
    if not java_home then
        raise("android_toolchain: no javac found in the downloaded JDK archive")
    end

    os.tryrm(jdk_dir)
    os.mv(java_home, jdk_dir)
    cprint("${green}JDK installed${clear} -> %s", jdk_dir)
end

local function provision_cmdline_tools(sdk_dir, staging)
    local os_label = host_os()
    local url = format("https://dl.google.com/android/repository/commandlinetools-%s-%s_latest.zip",
        (os_label == "windows") and "win" or os_label, CMDTOOLS_BUILD)

    local zip = path.join(staging, "cmdline-tools.zip")
    local extract_dir = path.join(staging, "cmdline-tools-extract")

    cprint("${bright}Downloading Android command-line tools %s...${clear}", CMDTOOLS_BUILD)
    http.download(url, zip)
    os.tryrm(extract_dir)
    archive.extract(zip, extract_dir)

    local latest = path.join(sdk_dir, "cmdline-tools", "latest")
    os.tryrm(latest)
    os.mkdir(path.directory(latest))
    os.mv(path.join(extract_dir, "cmdline-tools"), latest)
    cprint("${green}command-line tools installed${clear} -> %s", latest)
end

local function sdkmanager_path(sdk_dir)
    local bin = is_host("windows") and "sdkmanager.bat" or "sdkmanager"
    return path.join(sdk_dir, "cmdline-tools", "latest", "bin", bin)
end

local function run_sdkmanager(sdk_dir, jdk_dir, args, opts)
    local envs = { JAVA_HOME = jdk_dir }
    envs.PATH = path.join(jdk_dir, "bin") .. path.envsep() .. (os.getenv("PATH") or "")
    local argv = table.join({ "--sdk_root=" .. sdk_dir }, args)
    os.execv(sdkmanager_path(sdk_dir), argv, table.join2({ envs = envs }, opts or {}))
end

local function provision_sdk_packages(sdk_dir, jdk_dir)
    local yes_file = os.tmpfile()
    io.writefile(yes_file, string.rep("y\n", 64))
    cprint("${bright}Accepting Android SDK licenses...${clear}")
    run_sdkmanager(sdk_dir, jdk_dir, { "--licenses" }, { stdin = yes_file })
    os.tryrm(yes_file)

    cprint("${bright}Installing SDK packages (platform-tools, android-%s, build-tools %s, ndk %s)...${clear}",
        ANDROID_API, BUILD_TOOLS, NDK_VERSION)
    run_sdkmanager(sdk_dir, jdk_dir, {
        "platform-tools",
        "platforms;android-" .. ANDROID_API,
        "build-tools;" .. BUILD_TOOLS,
        "ndk;" .. NDK_VERSION,
    })
end

local function sdk_complete(sdk_dir)
    local bt = is_host("windows") and "aapt2.exe" or "aapt2"
    return os.isfile(path.join(sdk_dir, "build-tools", BUILD_TOOLS, bt))
        and os.isfile(path.join(sdk_dir, "platforms", "android-" .. ANDROID_API, "android.jar"))
        and os.isdir(path.join(sdk_dir, "ndk", NDK_VERSION))
end

function provision(opts)
    opts          = opts or {}
    local android = _smol_android_dir()
    local jdk_dir = path.join(android, "jdk")
    local sdk_dir = path.join(android, "sdk")
    local staging = path.join(android, ".staging")

    os.mkdir(android)
    os.tryrm(staging)
    os.mkdir(staging)

    local javac = path.join(jdk_dir, "bin", is_host("windows") and "javac.exe" or "javac")
    if os.isfile(javac) and not opts.force then
        cprint("${dim}JDK already present -> %s${clear}", jdk_dir)
    else
        provision_jdk(jdk_dir, staging)
    end

    if not os.isfile(sdkmanager_path(sdk_dir)) or opts.force then
        provision_cmdline_tools(sdk_dir, staging)
    else
        cprint("${dim}command-line tools already present${clear}")
    end

    if sdk_complete(sdk_dir) and not opts.force then
        cprint("${dim}SDK packages already present (platform/build-tools/ndk)${clear}")
    else
        provision_sdk_packages(sdk_dir, jdk_dir)
    end

    os.tryrm(staging)

    local ndk_dir = path.join(sdk_dir, "ndk", NDK_VERSION)
    cprint("")
    cprint("${bright green}Android toolchain ready.${clear}")
    cprint("  JDK: %s", jdk_dir)
    cprint("  SDK: %s", sdk_dir)
    cprint("  NDK: %s", ndk_dir)
    cprint("")
    cprint("Configure an Android build with:")
    cprint("  ${bright}xmake f -p android -a arm64-v8a --ndk=%s${clear}", ndk_dir)
    cprint("  ${bright}xmake${clear}")
end
