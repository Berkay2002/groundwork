# Status (last updated: 2026-06-11, after Batch F)

## Where the project stands

All planned batches through F **plus all A–D addenda** are done and verified:

| Milestone | Contents | State |
|---|---|---|
| MVP (Phases 1–2) | Chunked world, terrain, face-culled meshing, FPS player with collision, break/place via raycast, streaming, chunk saving | done |
| Batch A | On-screen debug overlay (font8x8), hotbar UI, player persistence, `settings.cfg`, versioned save headers | done |
| Batch B | Trees, Wood/Leaves/Sand/Bedrock blocks, two-scale terrain (plains + hill mask), sand basins, bedrock floor | done |
| Batch C | Frustum culling, background generation + meshing on a worker pool, perf counters in overlay | done |
| Batch D | 4-bit sun + block light per cell, BFS add/unlight relight on edits, cross-border propagation, light baked into mesh verts (0.85^n curve), Torch block as a 3D post (walk-through, non-opaque, emits 14), light readout in overlay | done |
| Batch E | Spaghetti caves (two 3D noise fields, surface-pinched), Coal/Iron ore veins (depth-banded, per-8³-cell hashing), water (lake basins to SEA_LEVEL 20, translucent second mesh pass, sunlight attenuation, swim physics, hotbar slot 8) | done |
| A–D addenda | level.bin seed file, atomic saves, autosave; block registry table; `--bench` + golden-screenshot test; per-vertex AO + smooth lighting (fog pre-existed) | done |
| Batch F | Greedy meshing (AO/light-tuple keyed), 12-byte packed vertices + texture array, frame-budgeted prioritized mesh uploads, front-to-back/back-to-front draw sorting | done |

### Batch F implementation notes (2026-06-11)

- **Greedy meshing**: `buildMeshData` sweeps each of the 6 face directions
  as slices (`AXES` table maps face → slice/grid axes with texture-direction
  signs derived from the old FACES winding). Cells key on
  `(block id, 4 quantized corner brightness bytes)`; maximal rectangles of
  equal keys merge. Because the key contains the *exact* per-corner shading,
  merging can never change the rendered lighting — this is the Batch D
  constraint, enforced structurally. Water merges too (flat per-face light
  keys); torches are excluded and keep their custom post geometry. Sweeps
  clamp to the highest non-air layer and use strided direct neighbor reads
  on interior slices: 0.40 ms/chunk (naive greedy was 1.36).
- **Packed vertices**: `ChunkVertex` is 12 bytes — u16 x/y/z and u16 u/v in
  **1/16 units** (chunk-local positions; torch fractions stay exact), u8
  brightness (×255), u8 texture-array layer. The shader gets integer
  attribs (`glVertexAttribIPointer`) plus a per-draw `uOrigin` uniform
  (location passed into `World::drawChunks`). UVs span `0..w`/`0..h` tiles
  on merged faces, wrapped by `GL_TEXTURE_2D_ARRAY` + REPEAT
  (`createBlockTextureArray`; the HUD still uses the 2D strip atlas — both
  come from the shared `tilePixel`). Near spawn: ~9.5k → ~1.7k verts/chunk,
  222 KB → 20 KB vertex data.
- **Budgeted uploads**: `processMeshing(budget, playerPos, frustum*, ms)`
  drains worker results into `uploadQueue_` (newest per chunk, unloaded
  dropped), frees the pipeline slot immediately, and uploads best-priority
  first (in-frustum, then nearest) within `UPLOAD_BUDGET_MS` = 3 ms, always
  ≥1/frame. Dirty-chunk enqueueing uses the same priority (it used to be
  hash-map order — edits near the player no longer wait behind streaming).
  main.cpp computes the camera/frustum *before* processMeshing now.
- **Decisions by measurement**: VBO pooling/orphaning skipped — one full
  chunk upload is ~12 µs post-greedy, nothing to save. Occlusion heuristics
  skipped — ~80 draws/frame at ~340 fps is not draw-bound. "One persistent
  VAO" interpreted as per-chunk VAOs (GL 3.3 has no separate attrib-binding
  state; one VAO bind per chunk is already minimal).
- **Golden reference regenerated**: the packed-coordinate path
  (`uOrigin + pos/16`) shifts rasterization sub-pixel at block edges —
  ~3.5% of pixels moved (all on edges, verified via amplified diff map;
  solid areas identical). The reference was regenerated after visual
  inspection; the test now passes at ~0.03% noise again.

### A–D addenda implementation notes (2026-06-11)

- **Save safety (A)**: `World::loadOrCreateSeed` reads/writes
  `saves/world1/level.bin` (`MCLV` + u32 version + u32 seed); a caller seed
  only matters for brand-new worlds. All save files (chunk/player/level) go
  through `atomicSave` in `src/SaveIO.h` (write `.tmp`, `rename()`). A 30 s
  autosave timer in main.cpp saves modified chunks + player; unload-time
  saving already existed and is now pinned by `testUnloadSaves`.
- **Block registry (B)**: `BLOCK_DEFS` constexpr table in `Block.h`; the
  old predicate helpers (`isSolid`/`isOpaque`/…, `tileFor`, `blockName`)
  remain as thin table lookups, so call sites didn't change. `hardness`
  (255 = unbreakable) and `drop` are recorded for Batch G but unused.
  `loadChunkFromDisk` clamps out-of-range saved bytes to Air because the
  bytes index straight into the table.
- **AO + smooth lighting (D)**: in `buildMeshData`, each cube-face vertex
  gets brightness = directional face shade × average light of the open
  cells around its corner × 3-neighbor AO (`AO_CURVE` {0.55,0.72,0.86,1}).
  Quads split along the darker diagonal (anisotropy fix) — so triangle
  index order is no longer fixed, but vertex order is, which is what the
  mesh tests rely on. `ChunkSnapshot` gained four diagonal **corner
  columns** (blocks + light, `[y]`-indexed, empty = air/sunlit) because
  corner vertices sample diagonally out of the chunk; `World::snapshot`
  fills them, `markNeighborsDirty` is now 8-directional, and
  `markBorderDirty` (shared by `setBlock`/`setLight`) dirties the diagonal
  neighbor for corner cells. Torch keeps its fullbright special case;
  **water stays flat-lit per face** (a lake reads as one even sheet).
  Greedy meshing (Batch F) must merge only faces with identical corner
  AO/light — that data is per-vertex now.
- **Bench + golden (C)**: `--bench N` runs N frames with vsync forced off
  and prints fps + chunk/worker counters. `tests/golden_screenshot.py`
  (registered in ctest, `SKIP_RETURN_CODE 77` without a display) runs
  `--frames 400` from a fixed crafted viewpoint in a temp dir and compares
  to `tests/golden/reference.png` (≤1.5% pixels differing, ≤2.0 mean
  delta; observed noise ~0.03% from the overlay fps text). **Regenerate
  the reference (`--update`) after every intentional visual change** —
  Batch F's greedy mesher should produce an identical image, which makes
  this test the main safety net there.

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

**Batch G — Items, Inventory & Entity Foundation** is next per the agreed
order (`TODO.md`): fixed-timestep simulation tick first (the multiplayer
insurance), then a minimal entity system reusing the player's collision,
then block drops as the first entity, hotbar counts, inventory UI, and
player-save v2 for persistence. The Block registry's `hardness`/`drop`
columns have been waiting for this batch since the B addendum.

**Do not start a batch unsolicited** — see `WORKFLOW.md`.

## Environment facts

- Ubuntu 24.04, gcc 13, CMake 3.28, GLFW 3.3.10 + GLM as system packages,
  X11 display `:0` available (real session, no xvfb installed).
- Git repository (initialized by the user on 2026-06-11). `build/`, `saves/`,
  `screenshot.*`, and `settings.cfg` are gitignored transient state.
- World seed fixed at 1337 in `main.cpp`.
- Internet access works (font8x8 was fetched via curl).

## Recent verification snapshot (Batch F)

Warning-free build; `world_tests` (new: greedy merging/UV-span/determinism,
torch mesh, rewritten position-keyed AO/smooth tests) passes 3× and under
TSAN (ASLR workaround). Identical-viewpoint `--bench 3000` at render
distance 6: 299 fps (old mesher, same camera) → ~340 fps (run-to-run ±5%);
mesh build 0.40 ms/chunk (was 0.24 pre-greedy, 1.36 unoptimized greedy);
upload ~12 µs/chunk; queues empty at steady state. Mesh size near spawn:
1711 verts/chunk avg (was 9455), 20 KB vertex + 10 KB index data (was
222 + 55). Verified visually: standard golden view (AO/fog/trees intact, no
greedy cracks at quad T-junctions), lake panorama (water one even sheet, no
seams between merged quads), underwater looking up through the surface.
Golden test stable at ~0.03% noise against the regenerated reference.

## Older verification snapshot (A–D addenda)

Warning-free build; `world_tests` (now incl. level-seed, unload-save,
registry, AO, corner-column AO, smooth-lighting tests) passes repeatedly and
under TSAN (ASLR workaround). `--bench 300`: ~360 fps vsync-off at render
distance 6, gen 0.51 ms, mesh 0.26 ms (AO/smooth lighting cost ~0.05 ms of
meshing). Golden screenshot stable at 0.027% pixel noise across runs.
Verified visually: AO contact shadows at terrain ledges and under tree
canopies, smooth gradients on the ground, fog fading distant chunks into
the sky with no pop-in edge.

## Older verification snapshot (Batch E)

At Batch E completion: 120 fps at render distance 6, gen ~0.5 ms/chunk
(caves + ores + water roughly doubled it again, still trivial), mesh
~0.2 ms, queues empty at steady state. Verified visually: lake panorama and
sandy shore with translucent shallows, underwater view up through the
surface, torch-lit cave (`sun 0 torch 13` readout), sunlight gradient down
a natural cave shaft, iron vein by torchlight. Tests (incl. new cave/ore/
water generation, water predicates, water mesh and underwater-sunlight
cases) pass repeatedly and under ThreadSanitizer (ASLR workaround).
