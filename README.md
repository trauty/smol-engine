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

### Engine

```bash
xmake f -m debug          # configure
xmake                     # engine + cooker + editor + runtime; engine assets cook automatically
xmake run smol-editor     # launch the editor
```

The static engine is only needed for standalone game builds, so it is skipped by
a normal build:

```bash
xmake build smol-engine-static
```

Tracy profiling is off by default:

```bash
xmake f -m releasedbg --profiling=y
xmake
```

### Game project

A game locates the engine while xmake reads the project, in this order:

1. `SMOL_ENGINE_DIR`
2. `./smol-engine` (vendored or submodule)
3. `../smol-engine` (sibling checkout)
4. newest install under `~/.smol/engines/`

**Bash:**
```bash
export SMOL_ENGINE_DIR=/path/to/smol-engine
xmake f -m debug
xmake                     # -> bin/<game>.so; game assets cook to .smol/game
```

**Powershell:**
```powershell
$env:SMOL_ENGINE_DIR = "C:\path\to\smol-engine"
xmake f -m debug
xmake                     # -> bin\<game>.dll
```

Open the project in the editor to run it. The editor loads the game library at
runtime and hot-reloads it whenever you rebuild, so `xmake` in the game project
is the whole edit loop.

### Standalone (no editor)

Links the static engine, your game code and the runtime entry point into one
binary. Build the static engine once, then build the game with `--standalone=y`:

```bash
# in the engine repo
xmake f -m release && xmake build smol-engine-static

# in the game project
xmake f --standalone=y -m release
xmake                     # -> build/<plat>/<arch>/<mode>/{<game>, assets/}
```

`--standalone` is a game-side option: it selects which engine to link, and is
not accepted in the engine repo.

### Cooking assets

Assets cook as part of a normal build.

The engine cooks its own assets into `build/<plat>/<arch>/<mode>/assets/engine`.
A game project then keeps one cooked root of its own, `<project>/.smol/`, holding
`engine/` (copied in from the engine build), `game/` (cooked from the project's
`assets/`), and a single `guid_map.json` covering both. The editor, the runtime
and a packaged build all read that one root.

The two stay in separate vfs namespaces -- `engine://assets/...` and
`game://assets/...` -- so the engine can address its own assets by absolute path
from inside a library shared by every game, and generic names like
`shaders/util.slang` cannot collide.

To force just the engine cook step:

```bash
xmake build smol-assets   # engine repo
```

## Packaging & distribution (experimental)

### Standalone

Bundle the self-contained standalone binary with its cooked assets into
`dist/<project>/`, ready to hand to a player. Run from the game project:

```bash
xmake f --standalone=y -m release   # static build
xmake                               # builds + cooks
xmake smol-package                  # -> dist/<project>/{<game>, assets/}
```

### Install the SDK (engine + editor + cooker)

The install mirrors the source layout, so a game consumes an SDK exactly the way
it consumes a checkout.

**Bash:**
```bash
xmake f -m release
xmake install -o ~/.smol/engines/$(cat VERSION)
```

**Powershell:**
```powershell
xmake f -m release
xmake install -o "$env:USERPROFILE\.smol\engines\$(Get-Content VERSION)"
```
