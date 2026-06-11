# Status (last updated: 2026-06-11, after Batch E)

## Where the project stands

All planned batches through E are **done and verified**:

| Milestone | Contents | State |
|---|---|---|
| MVP (Phases 1–2) | Chunked world, terrain, face-culled meshing, FPS player with collision, break/place via raycast, streaming, chunk saving | done |
| Batch A | On-screen debug overlay (font8x8), hotbar UI, player persistence, `settings.cfg`, versioned save headers | done |
| Batch B | Trees, Wood/Leaves/Sand/Bedrock blocks, two-scale terrain (plains + hill mask), sand basins, bedrock floor | done |
| Batch C | Frustum culling, background generation + meshing on a worker pool, perf counters in overlay | done |
| Batch D | 4-bit sun + block light per cell, BFS add/unlight relight on edits, cross-border propagation, light baked into mesh verts (0.85^n curve), Torch block as a 3D post (walk-through, non-opaque, emits 14), light readout in overlay | done |
| Batch E | Spaghetti caves (two 3D noise fields, surface-pinched), Coal/Iron ore veins (depth-banded, per-8³-cell hashing), water (lake basins to SEA_LEVEL 20, translucent second mesh pass, sunlight attenuation, swim physics, hotbar slot 8) | done |

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

### Batch E implementation notes

- New blocks (append-only, save stays v1): `CoalOre = 9`, `IronOre = 10`,
  `Water = 11`; atlas grew to 13 tiles. Water's predicate row: not solid
  (raycast passes through; placing into water replaces it), not collidable,
  not opaque, and a new `dimsSunlight` predicate breaks the lossless
  downward-15 rule (lakes darken ~1 level per block of depth; both the chunk
  BFS and `World::addLight` check the destination block, and the sun column
  fill stops at water). The water plan is `docs/plans/batch-e-water.md`.
- Caves: `Terrain::isCarved(wx,wy,wz,height)` — carve where **two** 3D fBm
  fields are both near zero (|n| < 0.085·taper, freq 1/48, y squashed 1.6×).
  The taper (clamp(depth/12, 0.3, 1)) pinches tunnels near the surface; lake
  and shore columns (height < SEA_LEVEL+2) are never carved above height-4.
  Trees skip candidates whose base column is carved (no floating trees).
- Lake basins: a third height mask (`basin_`, salt 0x5A17BEEF) sinks terrain
  up to 12 blocks where fbm > 0.42. **The salt was chosen so the origin area
  (the user's home chunks) gets zero depression** — terrain there is
  unchanged from Batch D apart from caves/ores. ~7% of land near origin is
  lake; the nearest is around (-14,-26), a big one around (-200,-200).
- Water rendering: `MeshData` gained `waterVerts/waterInds`; water faces are
  emitted only against air/torch (water-water and water-opaque culled), into
  a second VAO drawn by `World::drawWater` after all opaque chunks with
  GL_BLEND on, face culling off (surface visible from underwater), uAlpha
  0.65, depth writes on.
- Ore veins: per-(8³ cell, ore type) hash candidate → 3×3×3 clumpy blob that
  replaces stone only. Cells align with chunks so veins never cross borders.
  Coal: vein centers ≤ y 44, ~43% of cells; iron: ≤ y 22, ~31%.
- Swim: in water, gravity -10, terminal -4, Space sets vel.y = 4.5,
  horizontal speed ×0.6. Without this a deep lake would be inescapable
  (jump impulse only clears ~1.5 blocks).

## What's next

**Batch F — Mesh & Render Optimization** (greedy meshing, vertex packing,
draw ordering) is next per the agreed order D → E → F → G → H; reasoning at
the bottom of `TODO.md`. Note water changed the meshing rules (second
translucent mesh) — greedy meshing must handle both meshes.

**Do not start a batch unsolicited** — see `WORKFLOW.md`.

## Environment facts

- Ubuntu 24.04, gcc 13, CMake 3.28, GLFW 3.3.10 + GLM as system packages,
  X11 display `:0` available (real session, no xvfb installed).
- Git repository (initialized by the user on 2026-06-11). `build/`, `saves/`,
  `screenshot.*`, and `settings.cfg` are gitignored transient state.
- World seed fixed at 1337 in `main.cpp`.
- Internet access works (font8x8 was fetched via curl).

## Recent verification snapshot

At Batch E completion: 120 fps at render distance 6, gen ~0.5 ms/chunk
(caves + ores + water roughly doubled it again, still trivial), mesh
~0.2 ms, queues empty at steady state. Verified visually: lake panorama and
sandy shore with translucent shallows, underwater view up through the
surface, torch-lit cave (`sun 0 torch 13` readout), sunlight gradient down
a natural cave shaft, iron vein by torchlight. Tests (incl. new cave/ore/
water generation, water predicates, water mesh and underwater-sunlight
cases) pass repeatedly and under ThreadSanitizer (ASLR workaround).
