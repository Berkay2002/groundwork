# Groundwork (C++ / OpenGL)

A small voxel game: walk around a procedurally generated chunk world, break and
place blocks, and your edits persist between sessions.

## Quickstart

Linux:

```sh
sudo apt install build-essential cmake pkg-config libglfw3-dev libglm-dev
cmake -B build -S .
cmake --build build -j
./build/groundwork
```

Windows 11:

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
.\build\groundwork.exe
```

That's the whole install. Voxel block textures and the HUD font are generated
at startup, and sound effects are embedded in the binary. Authored character
content now lives under `assets/` and is loaded through a manifest. Requires a
C++17 compiler, CMake ≥ 3.16, GLFW 3, GLM, and OpenGL 3.3.

For a stripped release binary:

```sh
cmake --install build --strip --prefix dist   # -> dist/bin/groundwork
```

On Windows:

```powershell
cmake --install build --prefix dist           # -> dist\bin\groundwork.exe + glfw3.dll
```

Install also copies the runtime `assets/` directory beside `bin/`.

Sound can be compiled out with `-DENABLE_AUDIO=OFF` at configure time (the
game also simply stays silent when no audio device can be opened).

Run the headless world-logic tests with `./build/world_tests` on Linux or
`.\build\world_tests.exe` on Windows.

`./build/groundwork --frames 300` runs 300 frames, saves `screenshot.ppm`, and
exits (useful for automated checks). `./build/groundwork --bench 300` runs 300
frames with vsync forced off, prints performance counters (fps, chunks
drawn/loaded, mesh uploads, worker timings), and exits.

`./build/groundwork --demo-creature --frames 300` spawns the first authored
blocky character model in a save-isolated demo run. It writes a screenshot but
must not create, update, or delete files under `saves/world1`; the Windows
check is `powershell -ExecutionPolicy Bypass -File tests\demo_save_isolation.ps1`.

`ctest --test-dir build` additionally runs a golden-screenshot regression
test (`tests/golden_screenshot.py`) when Python is available. It renders a
fixed viewpoint and compares it against `tests/golden/reference.png`; it skips
without a display or Pillow/PIL.
After an intentional visual change, regenerate the reference with
`python3 tests/golden_screenshot.py --update build/groundwork tests/golden/reference.png`.
On Windows, use `python` and the `.exe` path:
`python tests\golden_screenshot.py --update build\groundwork.exe tests\golden\reference.png`.

## Controls

| Input | Action |
|---|---|
| Mouse | Look around |
| W / A / S / D | Move |
| Space | Jump (fly up in fly mode) |
| Left Shift | Fly down (fly mode) |
| Left Ctrl | Sprint |
| Left click | Break block |
| Right click | Place block, or open a crafting table/furnace in survival |
| 1–8, scroll wheel | Select hotbar slot |
| E | Open/close inventory (survival mode) |
| F | Toggle fly mode |
| M | Toggle survival/creative mode |
| F3 | Toggle the debug overlay |
| Esc | Pause menu — resume / settings / quit (closes the inventory first) |

Movement, jump, sneak, sprint, fly, inventory, and mode toggle are rebindable
through the `key_*` entries in `settings.cfg` (single letters/digits or names
like `SPACE`, `TAB`, `LSHIFT`, `LCTRL`, `CAPSLOCK`). The pause menu's Settings
page edits render distance, FOV, mouse sensitivity, volume, and vsync live and
writes them back to `settings.cfg`.

A debug overlay (FPS, position, current chunk, drawn/loaded chunk counts,
generation and meshing timings with queue depths, targeted block and the
sun/torch light at its face) is drawn in the top-left corner, and a hotbar
with the selected block at the bottom.

## Settings

`settings.cfg` is created next to the executable on first run:
`mouse_sensitivity`, `fov`, `render_distance` (chunks), `vsync`, `survival`,
`volume` (0–1), and the `key_*` bindings listed above. Most of these are
also editable in-game from the pause menu (Esc → Settings).

`survival=1` is the default. Fresh worlds use timed block breaking, finite
hotbar stacks, item drops, crafting, furnace smelting, tool durability, and
Minecraft-like harvest tiers. An old settings file with `survival=0` still
loads in creative mode: the hotbar is a fixed infinite palette and breaking
destroys blocks outright. Press `M` to switch modes at runtime; the setting is
written back to `settings.cfg`.

Survival progression starts from logs: craft planks, sticks, a crafting table,
wooden tools, stone tools and furnace, coal/torches, raw iron/iron ingots, and
finally iron-tier diamond mining. Right-click a crafting table for 3×3 recipes
and a furnace for input/fuel/output slots. Shift-click quick-moves supported
stacks; right-click splits or places one item.

## How it works

- **World** (`src/world/World.h/.cpp`) — owns chunks in a hash map keyed by chunk
  coordinates. Streams chunks in around the player (nearest first) and unloads
  distant ones, saving any the player modified. Also does the voxel raycast
  (Amanatides & Woo) used for block targeting. Chunks outside the camera
  frustum are culled per frame (`src/render/Frustum.h`).
- **Background work** (`src/platform/JobQueue.h`) — terrain generation and chunk mesh
  building run on a small worker pool so the frame loop never stalls.
  Workers only ever see freshly created chunks or immutable snapshots
  (chunk blocks + neighbor edge slices); all chunk-map mutation and GL
  uploads stay on the main thread, with two mutex-guarded result queues as
  the only synchronization. Lifecycle: missing → generating → loaded+dirty →
  meshing → uploaded; edits set dirty again. Finished meshes upload within
  a 3 ms/frame budget — visible and nearby chunks first — so a streaming
  burst never stalls a frame, and dirty chunks re-mesh nearest-first.
- **Blocks** (`src/world/Block.h`) — a single constexpr registry table holds every
  per-block property (name, solidity, collision, opacity, light emission,
  hardness, drop, per-face atlas tiles); the gameplay code only reads it
  through small predicate helpers, and saved chunk bytes are the enum values
  (append-only).
- **Chunk** (`src/world/Chunk.h/.cpp`) — 16×80×16 block storage plus mesh building:
  only faces adjacent to non-opaque cells are emitted, with per-face shading
  and per-vertex smooth lighting + ambient occlusion baked into the
  vertices: each corner averages the light of the open cells around it and
  darkens by the classic 3-neighbor AO rule, so block edges and corners read
  as soft contact shadows. A greedy mesher then merges coplanar faces of the
  same block — but only when their corner AO/light values match exactly, so
  merging never changes the shading — and vertices are packed into 14 bytes
  (integer positions/UVs in 1/16 units, separate sun and block-light
  brightness bytes, texture layer), cutting a chunk's vertex data by
  roughly 10×. Block textures live in a
  texture array with repeat wrapping so merged faces can tile them. Chunks
  are marked dirty on edits (including neighbors across borders, diagonals
  included) and rebuilt nearest-first with a per-frame budget. Opaque chunks
  draw front-to-back (early-z), water back-to-front. Torches are meshed as
  thin 3D posts rather than cubes. Water builds a second, translucent mesh
  per chunk, drawn after all opaque geometry with blending (faces appear
  only against air, so a lake renders as one surface — visible from below
  too).
- **Lighting** — every cell stores 4-bit sunlight + 4-bit block light.
  Sunlight column-fills from the sky (level 15 falls without attenuation)
  and BFS-spreads into overhangs; torches emit block light 14 that fades by
  one per block. Water transmits light but breaks the lossless downward rule,
  so lakes darken one level per block of depth. Breaking/placing blocks
  relights incrementally (flood-fill add, unlight-BFS remove), propagating
  across chunk borders. Light is never saved — it is recomputed when a chunk
  is generated or loaded.
- **Day/night cycle** (`src/world/DayCycle.h`) — a 10-minute world day drives the
  sky/fog color (blue noon, orange dusk and dawn, near-black night) and a
  sun-level factor. Sun and block light are baked into the mesh as separate
  channels, and the shader takes `max(sun × sunLevel, block)` — so night
  falls over the whole world without relighting or remeshing a single chunk,
  and torches keep their full glow after dark. Moonlight keeps night terrain
  barely readable instead of pitch black. The day clock persists in
  `level.bin`, so a save resumes at the time of day you left it.
- **Audio** (`src/audio/Audio.cpp`, `src/audio/Sounds.h`, `src/audio/SoundData.h`) — block
  break/place sounds and footsteps are real recordings from
  [Kenney's CC0 "Impact Sounds" pack](https://kenney.nl/assets/impact-sounds),
  embedded in the binary as raw PCM (regenerate with
  `tools/make_sounddata.sh`) so the no-asset-files rule still holds. A small
  voice pool mixes them on a [miniaudio](https://miniaud.io) playback device
  (vendored single header) with a soft limiter; each play picks a random
  recorded variant plus a light pitch jitter so repeats don't sound
  mechanical. `volume` in settings.cfg (or the pause menu) scales everything.
- **Terrain** (`src/world/Terrain.cpp`, `src/world/Noise.h`) — deterministic and a pure
  function of world coordinates + seed: rolling value-noise plains plus
  occasional hill regions selected by a low-frequency mask; sandy basins below
  y=21, unbreakable bedrock at y=0. A second low-frequency mask sinks lake
  basins that fill with water up to y=20 (sandy shores and beds). Underground,
  "spaghetti" caves are carved where two 3D noise fields are both near zero —
  they pinch closed near the surface so cave mouths stay occasional, and never
  open the floor of a lake. Coal and iron veins (coal shallow-to-mid, iron
  deep) replace stone via one hashed vein candidate per 8³ cell. Trees are
  placed one-candidate-per-8x8-cell by hashing, so chunks generate
  independently in any order and trees that straddle a chunk border come out
  identical on both sides; everything stays order-independent the same way.
- **Player** (`src/sim/Player.h/.cpp`) — first-person controller with gravity,
  jumping, and swept per-axis AABB collision (sub-stepped so fast movement
  can't tunnel through blocks). Water is swim-through: gravity weakens, you
  sink slowly, and holding Space swims up. `F` toggles a fly mode for
  exploring. The collision itself lives in `src/sim/Physics.h/.cpp` as a reusable
  `Body`/`moveBody`, shared with entities.
- **Simulation tick** — all gameplay simulation (player physics, entities)
  runs at a fixed 20 ticks per second, decoupled from the frame rate;
  rendering interpolates positions between the last two ticks. This keeps
  physics deterministic across machines and is the groundwork a future
  multiplayer mode would need.
- **Authored assets** (`assets/`, `src/assets/*`) — runtime content that is not
  naturally procedural starts in `assets/`. `assets/manifest.json` maps stable
  ids such as `creature.kenney_wanderer` to model files; `AssetManager` parses
  the manifest, caches CPU-side model data, and keeps app/render code off
  hardcoded asset paths. The first imported content is Kenney's CC0 Blocky
  Characters `character-a.glb` with its external PNG texture and source/license
  notes beside it.
- **Entities** (`src/sim/Entity.h/.cpp`) — dropped items and living entities
  share the main-thread entity manager but use separate storage and queries.
  Dropped items are small textured cubes (`src/render/ItemRenderer.cpp`) that
  bob and spin, fall with shared AABB physics, magnetize to the player, stack
  into the inventory, freeze while their chunk is unloaded, despawn after 5
  minutes, and persist in chunk-scoped entity files. Living entities now have a
  body, previous position, health, tick state, model id, deterministic movement,
  terrain collision, damage/death, and a simple coal drop. They are not saved in
  this batch.
- **Model rendering** (`src/render/ModelRenderer.cpp`) — uploads manifest-loaded
  CPU-side GLB meshes and PNG textures to GL on the main thread, then draws
  living entities by model id. Simulation never owns asset files or GL objects.
- **Survival loop** (`src/sim/Mining.*`, `src/sim/Crafting.*`,
  `src/world/BlockEntity.*`) — registry-driven hardness, tool class, harvest
  tier, drops, and durability feed timed mining. Crafting recipes are pure
  logic for 2×2 inventory and 3×3 table surfaces. Furnaces are world-owned
  block entities with persisted input/fuel/output and raw-iron smelting.
- **Inventory** (`src/sim/Inventory.h`) — 4 rows × 8 columns of item stacks
  (max 64), row 0 doubling as the hotbar. Pure logic, exercised headlessly by
  the tests; the inventory/crafting/furnace UI is drawn with HUD primitives.
- **Textures** (`src/render/Texture.cpp`, `src/render/BreakOverlay.cpp`) — all
  block tiles, item icons, and mining crack stages are generated procedurally
  at startup. The same tile functions fill both a
  texture array (chunk rendering, repeat wrapping for merged faces) and a 2D
  atlas strip (HUD icons).
- **HUD** (`src/ui/Hud.cpp`) — 2D overlay renderer (debug text, hotbar,
  crosshair) using the public-domain `font8x8` bitmap font baked into a
  texture at startup.
- **Saving** — modified chunks are written as versioned block dumps
  (`MCCH` magic + version header) to `saves/world1/c_<x>_<z>.bin` on unload,
  on a 30-second autosave timer, and on exit; untouched chunks are
  regenerated from the seed, which lives in `saves/world1/level.bin`
  (alongside the day clock) so the world survives changes to the built-in
  default seed. Player position, look
  direction, fly mode, hotbar slot, and the survival inventory persist in
  `saves/world1/player.bin` (format v3; v1 files load with an empty inventory
  and v2 block stacks migrate to item stacks). Furnace block entities persist
  in `saves/world1/block_entities.bin`. All save files are written atomically
  (temp file + rename), and files with a bad/old header are rejected and
  regenerated rather than crashing.
