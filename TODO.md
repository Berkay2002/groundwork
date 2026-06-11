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

## Batch F — Mesh & Render Optimization

- [ ] Greedy meshing (merge coplanar same-tile faces **with identical corner
      AO/light values** — the Batch D addenda constraint; must handle both
      the opaque and water meshes; needs texture wrap handling — switch
      atlas strip to a texture array or per-face UV tiling)
- [ ] Vertex size reduction (pack position/uv/light/AO into ints; measure
      first via `--bench`)
- [ ] (new) Frame-budgeted main-thread work: cap mesh uploads per frame
      (~2–4 ms), prioritize by distance-to-player and frustum, drop results
      for chunks that unloaded meanwhile
- [ ] (new) GL buffer lifecycle: pool/orphan chunk VBOs instead of
      reallocating per remesh (measure first)
- [ ] Sort chunk draws front-to-back for early-z; one persistent VAO layout
- [ ] Occlusion heuristics only if profiling shows draw-bound (don't guess)

## Batch G — Items, Inventory & Entity Foundation

- [ ] (new) Fixed-timestep simulation tick (e.g. 20 TPS) for world/entity
      logic, decoupled from render with interpolation — the "don't bake in
      single-player assumptions" insurance for later multiplayer, far easier
      before entities exist than after
- [ ] (new) Minimal entity system: position, velocity, AABB reusing the
      player's sub-stepped collision, update + render lists, per-chunk
      bucketing
- [ ] Block drops: breaking yields an item — dropped items as the first
      entity (small bobbing cube, magnetized pickup), replacing the old
      direct-to-inventory plan
- [ ] Item counts in hotbar slots + finite placement (survival-ish mode
      flag; keep current infinite mode as creative)
- [ ] Full inventory grid UI (open/close with E, move stacks, Hud-based)
- [ ] Inventory persistence in player.bin (bump player save to v2)

## Batch H — Polish & Distribution

- [ ] Audio (block break/place, footsteps; miniaudio or OpenAL, keep optional)
- [ ] Pause menu (resume / settings / quit; settings editable in-game and
      written back to settings.cfg)
- [ ] Key rebinding via settings.cfg
- [ ] Day/night cycle (sky color + sun light level over time; fog color from
      the Batch D addenda follows the sky)
- [ ] Release build packaging (strip, assets-free binary, README quickstart;
      test build on a clean machine/container)

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
bench/golden screenshot, D's AO/smooth light/fog), then F → G → H. The D
addenda preceded F's greedy mesher because merging rules depend on corner
AO/light values; entities live inside G because dropped items are the gentle
on-ramp to mobs; polish last.
