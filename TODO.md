# Roadmap Checklist (v2)

Same F → G → H spine; the groundwork items were filed as addenda under the
done batch they conceptually extend (persistence under A, blocks under B,
perf/verification under C, lighting visuals under D) rather than as new
batches. **All A–D addenda are now done** (2026-06-11). Batch E reflects
what was actually built.

## Batch A — Usable Game Foundation — DONE (incl. addenda)

- [x] On-screen debug overlay, hotbar UI, player persistence, `settings.cfg`,
      versioned save headers
- [x] World metadata file (`saves/world1/level.bin`: `MCLV` + version +
      seed). Written on world creation, read back on load, so the save stays
      valid even if the default seed constant in `main.cpp` changes; bad/old
      files are rewritten with the fallback seed.
- [x] Atomic save writes: chunk/player/level files stream to `<name>.tmp`
      and `rename()` over the old file (`src/SaveIO.h`), so a crash
      mid-write can't corrupt real player data.
- [x] Autosave + save-on-unload audit: chunks streaming out were already
      saved (now covered by a test); a 30 s timer in the main loop also
      saves modified chunks + player, so a crash loses at most ~30 s.

## Batch B — Better Terrain — DONE (incl. addendum)

- [x] Trees, Wood/Leaves/Sand/Bedrock blocks, two-scale terrain, bedrock floor
- [x] Block registry table: constexpr `BLOCK_DEFS` in `Block.h` (name,
      solid/collidable/opaque/`dimsSunlight`, emission, hardness, drop,
      per-face tiles) replacing the predicate/`tileFor` switches. Hardness
      and drop are recorded for Batch G but unused until then; chunk loading
      clamps unknown saved ids to Air. File-loaded definitions remain the
      later modding step. Enum values stay the saved bytes: **append, never
      renumber** still applies.

## Batch C — Scalable World — DONE (incl. addenda)

- [x] Frustum culling, background gen + meshing on worker threads, perf
      counters in overlay
- [x] `--bench N` flag: run N frames with vsync forced off, print fps,
      drawn/loaded chunk counts, upload totals and gen/mesh worker times,
      exit — the before/after number for Batch F.
- [x] Golden-screenshot regression test: `tests/golden_screenshot.py` runs
      `--frames 400` from a fixed viewpoint in a temp world and compares
      against `tests/golden/reference.png` with a tolerance (overlay text
      noise ~0.03%, limit 1.5%). Registered in ctest; skips without a
      display. Regenerate after intentional visual changes with `--update`.

## Batch D — Lighting — DONE (incl. addenda)

- [x] 4-bit sun + block light, BFS relight on edits, cross-border
      propagation, light baked into mesh verts, Torch block
- [x] Per-vertex ambient occlusion (classic 3-neighbor corner darkening in
      `buildMeshData`; `ChunkSnapshot` gained diagonal corner columns; quad
      split follows the darker diagonal). Landed before greedy meshing as
      required — face merging must now compare corner AO/light.
- [x] Smooth lighting: vertex brightness averages the open cells around
      each corner (light can't leak around an edge: the diagonal cell is
      skipped when both sides are opaque). Water keeps flat per-face light
      so a lake stays one even sheet.
- [x] Distance fog matched to render distance — was already in the chunk
      shader since the MVP (0.7–0.98 of render distance, fading to the
      `uSky` uniform, which the Batch H day/night sky will drive); verified
      and ticked rather than rebuilt.

## Batch E — Underground & Richer Terrain — DONE

- [x] Spaghetti caves (two 3D fBm fields both near zero, surface-pinched
      taper, bedrock intact, lake/shore columns protected, no floating trees)
- [x] Ores (`CoalOre = 9`, `IronOre = 10`): per-8³-cell hashed vein
      candidates → clumpy blobs replacing stone only, depth-banded
      (coal ≤ y 44, iron ≤ y 22), chunk-aligned so veins never cross borders
- [x] Water (`Water = 11`): lake basins sunk to SEA_LEVEL 20 via a third
      height mask, translucent second mesh pass per chunk
      (`waterVerts`/`drawWater`, blend on, cull off, depth writes on),
      `dimsSunlight` attenuation (~1 level/block of depth), swim physics,
      hotbar slot 8
- [x] ~~Taller world~~ — skipped: caves fit fine in the 80-high world, and a
      CHUNK_HEIGHT bump would regenerate the player's modified chunks

## Batch F — Mesh & Render Optimization — DONE

- [x] Greedy meshing: coplanar same-block faces merge only when their
      quantized corner AO/light tuples are identical (the Batch D
      constraint), for both the opaque and water meshes; torches stay
      custom geometry. Texture wrap solved by moving chunk rendering to a
      GL_TEXTURE_2D_ARRAY (tile = layer, REPEAT wrapping); the HUD keeps
      the 2D strip atlas, both built from the same tile functions.
      ~9.5k → ~1.7k verts/chunk near spawn.
- [x] Vertex size reduction: 24-byte float vertex → 12-byte packed ints
      (u16 chunk-local position and uv in 1/16 units — torch geometry stays
      exact — u8 brightness, u8 layer; the chunk origin became a per-draw
      uniform). Combined with greedy: 222 KB → 20 KB vertex data per chunk;
      steady-state bench 299 → ~340 fps at render distance 6.
- [x] Frame-budgeted main-thread work: finished meshes wait in an upload
      queue and upload within a 3 ms/frame cap (`UPLOAD_BUDGET_MS`),
      in-frustum + nearest chunks first, dropped if their chunk unloaded,
      newest result kept per chunk; dirty-chunk *enqueueing* is also
      priority-ordered now (was hash-map order).
- [x] GL buffer lifecycle: measured first, as specified — a full chunk
      upload costs ~12 µs now that greedy meshes are ~30 KB, so VBO
      pooling/orphaning has nothing left to save. Skipped on the numbers.
- [x] Chunk draws sorted front-to-back for early-z; water back-to-front
      (blend-correct — it was unordered before). Per-chunk VAOs kept
      instead of "one persistent VAO": GL 3.3 has no separate
      attrib-binding state, so one VAO bind per chunk *is* the minimal
      call sequence.
- [x] Occlusion heuristics: bench shows we are nowhere near draw-bound
      (~80 draws/frame at ~340 fps), so skipped per the "don't guess" rule.

## Batch G — Items, Inventory & Entity Foundation — DONE

- [x] (new) Fixed-timestep simulation tick (20 TPS, `TICK_DT` accumulator in
      main.cpp) for player/entity logic, decoupled from render with
      position interpolation (`prevPos` + alpha) — the "don't bake in
      single-player assumptions" insurance for later multiplayer
- [x] (new) Minimal entity system (`src/Entity.{h,cpp}`): position, velocity,
      AABB reusing the player's sub-stepped collision (extracted to
      `src/Physics.{h,cpp}` as `Body`/`moveBody`), update + render lists,
      per-chunk bucketing. Entities freeze in unloaded chunks and are NOT
      persisted across runs (accepted Batch G limitation).
- [x] Block drops: breaking yields the registry's `drop` — dropped items as
      the first entity (small bobbing/spinning cube via
      `src/ItemRenderer.cpp`, magnetized pickup within ~2 blocks). Survival
      mode only; creative keeps destroy-outright.
- [x] Item counts in hotbar slots + finite placement (`survival=1` in
      settings.cfg; the default stays the original infinite creative mode)
- [x] Full inventory grid UI (open/close with E, click to move/swap/merge
      stacks, Hud-based, 4×8 with the hotbar as row 0)
- [x] Inventory persistence in player.bin (save bumped to v2; v1 files
      migrate with an empty inventory instead of being rejected)

## Batch H — Polish & Distribution — DONE

- [x] Audio: break/place/footstep effects synthesized at startup (no asset
      files), mixed by a small voice pool over the vendored miniaudio header;
      `-DENABLE_AUDIO=OFF` compiles it out, a missing device just means
      silence; `volume` in settings.cfg
- [x] Pause menu: Esc pauses the simulation behind a dim overlay
      (resume / settings / quit); the settings page edits render distance,
      FOV, sensitivity, volume, vsync live and writes settings.cfg on every
      change
- [x] Key rebinding via `key_*` entries in settings.cfg (movement, jump,
      sneak, sprint, fly, inventory; names per `KeyBinds.h`)
- [x] Day/night cycle: 10-minute world day drives sky/fog color and a
      sun-level uniform; vertex light carries separate sun/block channels so
      night needs no relighting/remeshing and torches stay bright; day clock
      persisted in level.bin v2 (v1 migrates keeping its seed)
- [x] Release packaging: `cmake --install build --strip --prefix dist`
      (449 KB self-contained binary), README quickstart, verified from
      scratch in a clean ubuntu:24.04 container

## Far later (only with a working foundation)

- [ ] Crafting (needs inventory from Batch G first)
- [ ] Mobs (pathing, spawning, combat — large; builds on the G entity system
      and fixed tick)
- [ ] Multiplayer (world authority, prediction — very large; the fixed tick
      from G is the down payment)
- [ ] Modding/scripting (the Batch B registry becomes file-loaded
      definitions; stable data formats first)

## Deliberately not added

Biomes, weather, real shadow mapping, taller/infinite world: none block the
Far-later items, and each is a batch-sized commitment. Revisit after H.

## Suggested order

~~Addenda first~~ (done: A's save safety + seed file, B's registry, C's
bench/golden screenshot, D's AO/smooth light/fog), ~~then F~~ (done:
greedy meshing, packed vertices, budgeted uploads, sorted draws), then
G → H. The D addenda preceded F's greedy mesher because merging rules
depend on corner AO/light values; entities live inside G because dropped
items are the gentle on-ramp to mobs; polish last.
