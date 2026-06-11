# AGENTS.md

This file provides guidance to agents when working with code in this repository.

## Project

Groundwork — a Minecraft-style voxel game in C++17/OpenGL 3.3, grown
incrementally from an MVP. Work is organized in user-approved batches tracked
in `ROADMAP.md`; session state, workflow, and hard-won gotchas live in
`docs/handoff/`. Read
`docs/handoff/STATUS.md` first when resuming work.

## Commands

```sh
cmake -B build -S .                 # configure (once, or after CMakeLists edits)
cmake --build build -j              # build both targets
./build/world_tests                 # headless logic tests (no GL context needed)
./build/groundwork                  # run the game (needs a display)
./build/groundwork --frames 300     # run N frames, dump screenshot.ppm, exit
ctest --test-dir build              # same tests via ctest
```

Windows 11 native build path:

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install Kitware.CMake Ninja-build.Ninja
```

Open "x64 Native Tools Command Prompt for VS 2022" or "Developer PowerShell
for VS 2022", then run:

```powershell
git clone https://github.com/microsoft/vcpkg "$env:USERPROFILE\vcpkg"
& "$env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat"
& "$env:USERPROFILE\vcpkg\vcpkg.exe" install glfw3 glm --triplet x64-windows
cmake -B build -S . -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:USERPROFILE\vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake --build build -j
.\build\world_tests.exe
.\build\groundwork.exe
ctest --test-dir build
```

There is no lint target; the build uses `-Wall` and should stay warning-free.
Tests are a single binary (`tests/test_world.cpp`, plain CHECK macros) — to run
one test, comment out calls in its `main()` or just run the whole binary
(it finishes in seconds).

Linux dependencies are system packages only: `libglfw3-dev`, `libglm-dev`,
OpenGL via Mesa (`GL_GLEXT_PROTOTYPES` + OpenGL::GL). Windows uses vcpkg
packages `glfw3` and `glm`, MSVC, and the platform OpenGL library. There is no
loader library dependency like GLAD/GLEW; `src/render/GLCompat.{h,cpp}` is the in-tree
Windows OpenGL function loader. There are **zero asset files** — block textures
and the HUD font are generated/embedded at startup. Keep that property.

## Architecture

Two-thread-pool game with a strict ownership rule:

**The main thread owns the chunk map and all GL. Workers never touch either.**

- `src/world/World.{h,cpp}` is the hub: a hash map `ChunkKey → unique_ptr<Chunk>`,
  chunk streaming around the player, the voxel raycast, save/load, and the two
  async pipelines. Generation jobs build a *fresh* `Chunk` off-thread and hand
  it back through the mutex-guarded `genDone_` queue; mesh jobs consume an
  immutable `ChunkSnapshot` (chunk blocks + 1-deep neighbor edge slices) and
  return CPU-side `MeshData` through `meshDone_`. `World::update()` integrates
  generated chunks; `World::processMeshing()` uploads finished meshes (GL) and
  enqueues dirty chunks. The `JobQueue` member is declared **last** in World so
  workers join before the queues are destroyed — preserve that ordering.
- Chunk lifecycle: missing → pendingGen → loaded+dirty → meshInFlight →
  uploaded. Edits set `dirty` again; a chunk edited while meshing is simply
  re-enqueued when the stale result returns. Border edits also dirty the
  neighbor chunk (both in `setBlock` and when a new chunk arrives).
- `src/world/Chunk.{h,cpp}`: 16×80×16 block storage (`CHUNK_SIZE`, `CHUNK_HEIGHT`).
  `buildMeshData(snapshot)` is a pure function (thread-safe, GL-free,
  face-culling mesher with per-face brightness baked into vertices);
  `Chunk::uploadMesh` is the GL half. Keep that split — it is what makes
  meshing background-safe and testable headless.
- `src/world/Terrain.{h,cpp}`: generation is a **pure function of world coordinates
  + seed** — never of chunk load order. This is why trees that straddle chunk
  borders come out identical from either side (one tree candidate per 8×8 cell
  via hashing, each chunk writes whatever overlaps it). Any new feature
  (caves, ores) must preserve this invariant. `Noise.h` is value-noise fBm.
- `src/app/main.cpp`: GLFW window/input, the frame loop (player update → world
  update → meshing → render → HUD), shaders as embedded strings, hotbar state,
  player save/load, and the `--frames` self-test flag.
- `src/ui/Hud.{h,cpp}`: pixel-space 2D overlay batching solid rects, block-atlas
  tiles, and bitmap-font text into three draws. Extend this for any new UI
  instead of ad-hoc GL.
- Support headers: `src/render/Frustum.h` (AABB culling, used in
  `World::drawChunks`), `src/platform/JobQueue.h` (worker pool),
  `src/platform/Settings.h` (`settings.cfg`), `src/render/Shader.h`,
  `src/sim/Player.{h,cpp}` (per-axis sub-stepped AABB collision — no
  tunneling), `src/world/Block.h` (block enum, `tileFor(block, face)` atlas
  mapping), `src/render/Texture.cpp` (procedural atlas).

## Save format rules

- Chunk files: `saves/world1/c_<x>_<z>.bin` = `"MCCH"` + u32 version + raw
  block bytes; only player-modified chunks are saved, everything else
  regenerates from the seed. Player file: `"MCPL"` + u32 version + pos/yaw/
  pitch + flying + hotbar slot. Bad/old headers are rejected and regenerated.
- Block enum values are the saved bytes: **append new blocks, never renumber.**
- Changing `CHUNK_HEIGHT` or block layout requires bumping `CHUNK_VERSION`.
- The user's `saves/` directory is real player data — never delete it without
  asking. (Deleting your *own* test-run saves is required cleanup.)

## Verification expectations

Every change: warning-free build + `world_tests` pass. Rendering changes:
run `--frames N`, convert `screenshot.ppm` to PNG, and look at it. Threading
changes: rerun tests several times and do a TSAN pass. Exact recipes
(including the crafted-player.bin viewpoint trick and the TSAN/ASLR
workaround) are in `docs/handoff/VERIFICATION.md`.
