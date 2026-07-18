# smol-engine

## Libs

* tinygltf v2.9.7
* VMA v3.4.0
* ImGui v1.92.8
* meshoptimizer v1.2
* JoltPhysics v5.6.0
* fmt v12.2.0
* stb_image v2.30
* stb_image_resize2 v2.18
* stb_image_write v1.16
* cglm v0.9.6
* nlohmann::json v3.12.0
* entt v3.16.0
* volk 1.4.335
* vulkan-headers 1.4.335
* libktx v4.4.2
* SDL 3.4.12
* slang v2026.12.0.1
* tracy v0.13.1

## Building

This project uses [xmake](https://xmake.io).

Build modes (`-m`): `debug` (default), `release`, `releasedbg`.

**Toolchain** (`--toolchain`): Linux defaults to `clang` (also `gcc`). Windows
defaults to `clang-cl` (also `msvc`). Pick one with `xmake f --toolchain=gcc`
/ `xmake f --toolchain=msvc`. To get MSVC without installing Visual Studio, run
`xmake smol-msvc-setup` first (still working on this, so won't work now)

### Desktop (editor)

```bash
xmake f -m debug          # configure
xmake                     # build engine + cooker + editor + runtime; assets cook automatically
xmake run smol-editor     # launch the editor
```

### Standalone (runtime, no editor)

Builds the engine and game statically into a single binary.

```bash
xmake f --standalone=y -m release
xmake
xmake run smol-runtime
```

### Cooking assets

Assets are cooked automatically as part of a normal build (the `smol-assets`
target). Engine assets cook to `<project>/.smol/engine`, game assets to
`<project>/.smol/game`.
To force just the cook step:

```bash
xmake build smol-assets
```

### Android (arm64-v8a)

Standalone is forced on Android, the whole engine + game link into one
`libmain.so` inside the APK.

**1. One-time setup:** Downloads the Android SDK, NDK (r27c),
and JDK 17 into `~/.smol/android`. This is the only command that ever downloads a
toolchain if requested.

```bash
xmake smol-android-setup  # add --force to redownload everything
```

**2. Create a debug keystore inside the root folder of the project:**

```bash
keytool -genkeypair -v -keystore debug.keystore -storepass android \
  -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 \
  -validity 10000 -dname "CN=Android Debug,O=Android,C=EU"
```

**3. Cook assets on the host first**: The Android APK bundles already cooked
assets, it does not run the cooker:

```bash
xmake f -m debug && xmake build smol-assets
```

**4. Configure for Android and build the APK:**
```bash
xmake f -p android -a arm64-v8a -m debug
xmake
```

To force a different NDK, pass `--ndk=<path>`.

## Packaging & distribution (experimental)

### Standalone

Bundle the self-contained standalone binary with its cooked assets into
`dist/<project>/`, ready to hand to a player:

```bash
xmake f --standalone=y -m release   # static build
xmake                               # builds + cooks
xmake smol-package                  # -> dist/<project>/{<game>, assets/}
```

### Install the SDK (engine + editor + cooker)

**Bash:**
```bash
xmake f --standalone=n -m release
xmake install -o ~/.smol/engines/$(cat VERSION)
```

**Powershell:**
```powershell
xmake install -o "$env:USERPROFILE\.smol\engines\$(Get-Content VERSION)"
```