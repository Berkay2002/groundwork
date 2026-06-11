# Status (last updated: 2026-06-11)

## Where the project stands

All planned batches through D are **done and verified**:

| Milestone | Contents | State |
|---|---|---|
| MVP (Phases 1–2) | Chunked world, terrain, face-culled meshing, FPS player with collision, break/place via raycast, streaming, chunk saving | done |
| Batch A | On-screen debug overlay (font8x8), hotbar UI, player persistence, `settings.cfg`, versioned save headers | done |
| Batch B | Trees, Wood/Leaves/Sand/Bedrock blocks, two-scale terrain (plains + hill mask), sand basins, bedrock floor | done |
| Batch C | Frustum culling, background generation + meshing on a worker pool, perf counters in overlay | done |
| Batch D | 4-bit sun + block light per cell, BFS add/unlight relight on edits, cross-border propagation, light baked into mesh verts (0.85^n curve), Torch block as a 3D post (walk-through, non-opaque, emits 14), light readout in overlay | done |

### Batch D implementation notes

- Light lives in `Chunk::light_` (sun low nibble, block high nibble), never
  saved (recomputed on gen/load → save format still v1, `Torch = 8` appended).
- Threading split: gen workers call `Chunk::computeInitialLight()` (column
  fill + intra-chunk BFS) on the fresh chunk; everything cross-chunk
  (`World::addLight/removeLight/seedChunkBorderLight`, called from
  `update()`/`setBlock()`) is main-thread only. `World::setLight` marks the
  owning chunk dirty (+ the neighbor when on a border) so meshes follow light.
- Sunlight level 15 propagates downward without attenuation (both add and
  remove BFS special-case this).
- The mesher lights each face by the cell it looks into (`ChunkSnapshot` now
  carries light + light edge slices; empty light = fully sunlit, which keeps
  bare test snapshots and missing neighbors bright). Torches are meshed as a
  2/16 × 10/16 post sampling the central strip of the torch tile, fullbright
  against its own emission; the selection outline shrinks to match;
  `isCollidable`/`isOpaque` are now distinct from `isSolid`.

## What's next

**Batch E — Underground & Richer Terrain** (caves, ores, water) is next per
the agreed order D → E → F → G → H; reasoning at the bottom of `TODO.md`.
Caves are the natural showcase for the new lighting. See `HANDOFF.md` in
this directory for session context and Batch E pointers.

**Do not start a batch unsolicited** — see `WORKFLOW.md`.

## Environment facts

- Ubuntu 24.04, gcc 13, CMake 3.28, GLFW 3.3.10 + GLM as system packages,
  X11 display `:0` available (real session, no xvfb installed).
- Git repository (initialized by the user on 2026-06-11). `build/`, `saves/`,
  `screenshot.*`, and `settings.cfg` are gitignored transient state.
- World seed fixed at 1337 in `main.cpp`.
- Internet access works (font8x8 was fetched via curl).

## Recent verification snapshot

At Batch D completion: still 120 fps at render distance 6, gen ~0.2 ms/chunk
(lighting roughly doubled generation cost, still trivial), mesh ~0.2 ms,
queues empty at steady state. Verified visually inside a sealed stone hut:
torch light pool with radial falloff into dark corners, sun shaft through the
doorway, overlay light readout (`sun 5 torch 13`) matching expectations; an
outdoor run confirmed sunlit terrain and under-canopy shading look right.
Tests (incl. new sunlight/torch/cross-border/unlight cases) pass repeatedly
and under ThreadSanitizer.
