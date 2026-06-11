# Gotchas and invariants (paid for already — don't rediscover)

## Threading

- Ownership rule: main thread owns the chunk map and all GL; workers only see
  freshly created chunks (generation) or immutable `ChunkSnapshot`s (meshing).
  The only locks in the codebase are the two result queues + the JobQueue's own.
- `World::pool_` is declared **last** so its destructor (which joins workers)
  runs before the result queues and Terrain are destroyed. Keep it last.
- Gen jobs create the `Chunk` inside the lambda — capturing a move-only
  `unique_ptr` would break `std::function`. Mesh snapshots are passed as
  `shared_ptr<ChunkSnapshot>` for the same reason.

## Rendering / texturing

- Atlas tile v=0 is sampled at the *bottom* of a block face. The grass-side
  tile compensates inside `Texture.cpp` (`fromTop` flip); `Hud::drawTile`
  flips v so HUD tiles appear upright. If a texture looks upside down, this
  is why.
- No GL loader library: `#define GL_GLEXT_PROTOTYPES` + `-lGL` (Mesa exports
  everything). Don't add GLAD/GLEW.
- The block-outline cube is expanded by 0.002 to avoid z-fighting; the chunk
  shader fogs toward the sky color between 0.7 and 0.98 of render distance.

## Terrain

- Generation must stay a pure function of (world coords, seed). Trees work
  cross-border because every chunk independently regenerates any tree
  overlapping it (one candidate per 8×8 cell, trunk kept ≥2 blocks from cell
  edge so a 2-block leaf radius is the worst-case overlap).
- Use `std::floor`, not `int()` truncation, when quantizing noise — truncation
  toward zero already caused a visible terrain bias around height 24 once.
- Tree trunks overwrite leaves (`overwrite=true`); leaves never overwrite
  terrain or trunk (`overwrite=false`). Order matters: leaves first, trunk last.

## Lighting (Batch D)

- Three distinct block predicates now — don't conflate them again:
  `isSolid` (raycast target / placement occupancy), `isOpaque` (stops light
  AND culls neighbor faces in the mesher), `isCollidable` (player movement).
  Torch is solid but neither opaque nor collidable. Water in Batch E will
  need its own row in this matrix — decide per predicate, not "is it air".
- Sunlight level 15 propagates downward with **no attenuation**. Both the add
  BFS and the unlight BFS special-case this (`d.y == -1 && level == 15`);
  forgetting it in one of the two leaves stale bright columns under new blocks.
- Light is never saved. `Chunk::computeInitialLight()` runs on the gen worker
  (fresh chunk only); ALL cross-chunk light work (`World::addLight/
  removeLight/seedChunkBorderLight`) is main-thread. Keep that split or TSAN
  will find you.
- `World::setLight` marks the owning chunk dirty and, for border cells, the
  adjacent chunk too — neighbor faces sample this cell's light. Mesh light
  bugs at chunk seams usually mean this marking was skipped.
- In `ChunkSnapshot`, an **empty light vector means fully sunlit (15)** —
  this keeps hand-built test snapshots and missing-neighbor edges bright.
  Don't "fix" it to 0; outdoor borders would flash dark while streaming.
- Emitter blocks light their own faces with at least their emission, and the
  torch mesh skips directional face shading — otherwise the flame looks dim.
- The torch is custom geometry (2/16 × 10/16 post) in `buildMeshData`, the
  selection outline in main.cpp shrinks to match, and its tile's central 2px
  strip is what the model samples — redraw the tile art with that in mind.

## Persistence

- Block enum values are the bytes on disk: append, never renumber.
- Loaders treat any bad magic/version/short-read as "regenerate" — corruption
  can't crash the game, but it also means silent regeneration; warnings go to
  stderr.
- After a terrain-generator change, old *modified* chunks remain as
  old-terrain islands (acceptable); old *player positions* may end up inside
  the new ground — `Player::ensureNotStuck` respawns to the surface, called
  right after `loadPlayer()` in main.

## Misc

- `src/font8x8_basic.h` is fetched from dhepper/font8x8 (public domain) and
  locally patched: declaration changed to `const unsigned char` to fix
  `-Wnarrowing`. Re-fetching it verbatim will reintroduce the build error.
- ESC behaves in two stages (release cursor → quit on second press) — easy to
  mistake for a bug.
- Physics dt is clamped to 0.05 s so window drags/stalls don't launch the
  player; horizontal motion is sub-stepped (≤0.45/step) to prevent tunneling.
- `--frames N` exists for automated runs/screenshots; keep it working — all
  visual verification depends on it.
