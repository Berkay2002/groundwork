# Minecraft Clone (C++ / OpenGL)

A small voxel game: walk around a procedurally generated chunk world, break and
place blocks, and your edits persist between sessions.

## Build & Run

Requires a C++17 compiler, CMake, GLFW 3, GLM, and OpenGL 3.3
(Ubuntu: `sudo apt install build-essential cmake libglfw3-dev libglm-dev`).

```sh
cmake -B build -S .
cmake --build build -j
./build/minecraft
```

Run the headless world-logic tests with `./build/world_tests`.

`./build/minecraft --frames 300` runs 300 frames, saves `screenshot.ppm`, and
exits (useful for automated checks).

## Controls

| Input | Action |
|---|---|
| Mouse | Look around |
| W / A / S / D | Move |
| Space | Jump (fly up in fly mode) |
| Left Shift | Fly down (fly mode) |
| Left Ctrl | Sprint |
| Left click | Break block |
| Right click | Place block |
| 1–8, scroll wheel | Select hotbar slot |
| F | Toggle fly mode |
| Esc | Release mouse, then quit |

A debug overlay (FPS, position, current chunk, drawn/loaded chunk counts,
generation and meshing timings with queue depths, targeted block and the
sun/torch light at its face) is drawn in the top-left corner, and a hotbar
with the selected block at the bottom.

## Settings

`settings.cfg` is created next to the executable on first run:
`mouse_sensitivity`, `fov`, `render_distance` (chunks), `vsync`.

## How it works

- **World** (`src/World.h/.cpp`) — owns chunks in a hash map keyed by chunk
  coordinates. Streams chunks in around the player (nearest first) and unloads
  distant ones, saving any the player modified. Also does the voxel raycast
  (Amanatides & Woo) used for block targeting. Chunks outside the camera
  frustum are culled per frame (`src/Frustum.h`).
- **Background work** (`src/JobQueue.h`) — terrain generation and chunk mesh
  building run on a small worker pool so the frame loop never stalls.
  Workers only ever see freshly created chunks or immutable snapshots
  (chunk blocks + neighbor edge slices); all chunk-map mutation and GL
  uploads stay on the main thread, with two mutex-guarded result queues as
  the only synchronization. Lifecycle: missing → generating → loaded+dirty →
  meshing → uploaded; edits set dirty again.
- **Chunk** (`src/Chunk.h/.cpp`) — 16×80×16 block storage plus mesh building:
  only faces adjacent to non-opaque cells are emitted, with per-face shading
  and the light level of the cell each face looks into baked into the
  vertices. Chunks are marked dirty on edits (including neighbors across
  borders) and rebuilt with a per-frame budget. Torches are meshed as thin
  3D posts rather than cubes. Water builds a second, translucent mesh per
  chunk, drawn after all opaque geometry with blending (faces appear only
  against air, so a lake renders as one surface — visible from below too).
- **Lighting** — every cell stores 4-bit sunlight + 4-bit block light.
  Sunlight column-fills from the sky (level 15 falls without attenuation)
  and BFS-spreads into overhangs; torches emit block light 14 that fades by
  one per block. Water transmits light but breaks the lossless downward rule,
  so lakes darken one level per block of depth. Breaking/placing blocks
  relights incrementally (flood-fill add, unlight-BFS remove), propagating
  across chunk borders. Light is never saved — it is recomputed when a chunk
  is generated or loaded.
- **Terrain** (`src/Terrain.cpp`, `src/Noise.h`) — deterministic and a pure
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
- **Player** (`src/Player.h/.cpp`) — first-person controller with gravity,
  jumping, and swept per-axis AABB collision (sub-stepped so fast movement
  can't tunnel through blocks). Water is swim-through: gravity weakens, you
  sink slowly, and holding Space swims up. `F` toggles a fly mode for
  exploring.
- **Textures** (`src/Texture.cpp`) — the block atlas is generated procedurally
  at startup (hash-noise grass/dirt/stone tiles), so there are no asset files.
- **HUD** (`src/Hud.cpp`) — 2D overlay renderer (debug text, hotbar,
  crosshair) using the public-domain `font8x8` bitmap font baked into a
  texture at startup.
- **Saving** — modified chunks are written as versioned block dumps
  (`MCCH` magic + version header) to `saves/world1/c_<x>_<z>.bin` on unload
  and on exit; untouched chunks are regenerated from the seed. Player
  position, look direction, fly mode, and hotbar slot persist in
  `saves/world1/player.bin`. Files with a bad/old header are rejected and
  regenerated rather than crashing.
