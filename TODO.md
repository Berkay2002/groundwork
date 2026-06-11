# Roadmap Checklist (v2 — proposed)

Proposed update of `TODO.md`. Same F → G → H spine; the new groundwork items
are filed as unchecked addenda under the done batch they conceptually extend
(persistence under A, blocks under B, perf/verification under C, lighting
visuals under D) rather than as new batches. Batch E reflects what was
actually built.

## Batch A — Usable Game Foundation — DONE (+ addenda)

- [x] On-screen debug overlay, hotbar UI, player persistence, `settings.cfg`,
      versioned save headers
- [ ] (new) World metadata file (`saves/world1/level.bin`: magic + version +
      seed). The seed is hardcoded in `main.cpp`, so a saves directory is only
      valid for a binary with that constant — unmodified chunks regenerate
      from the seed, making the save corrupt-by-design if it changes. Read
      seed from the file, write it on world creation.
- [ ] (new) Atomic save writes: write chunk/player/level files to a temp name
      and `rename()` over the old file, so a crash mid-write can't corrupt
      real player data.
- [ ] (new) Autosave + save-on-unload audit: verify edited chunks are saved
      when they stream out and on a periodic timer, not only at clean exit.

## Batch B — Better Terrain — DONE (+ addendum)

- [x] Trees, Wood/Leaves/Sand/Bedrock blocks, two-scale terrain, bedrock floor
- [ ] (new) Block registry table: id → name, opacity, collidability, emission,
      `dimsSunlight`, per-face tile, hardness, drop — replacing the growing
      predicate/`tileFor` switches in `Block.h`. Prerequisite for Batch G
      (drops, hardness) and later modding. Compile-time table (constexpr
      array) for now; file-loaded definitions are the later modding step.
      Enum values stay the saved bytes: **append, never renumber** still
      applies.

## Batch C — Scalable World — DONE (+ addenda)

- [x] Frustum culling, background gen + meshing on worker threads, perf
      counters in overlay
- [ ] (new) `--bench` flag: run N frames, print perf counters (gen/mesh
      times, draw counts) and exit — the before/after number for Batch F.
- [ ] (new) Golden-screenshot regression test: `--frames N` + fixed seed
      already give deterministic output; compare against a stored reference
      image with a tolerance, catching rendering regressions mechanically.

## Batch D — Lighting — DONE (+ addenda)

- [x] 4-bit sun + block light, BFS relight on edits, cross-border
      propagation, light baked into mesh verts, Torch block
- [ ] (new) Per-vertex ambient occlusion (classic 3-neighbor corner
      darkening, computed in `buildMeshData` from the existing
      `ChunkSnapshot`). **Must land before greedy meshing** — it changes
      which faces are mergeable.
- [ ] (new) Smooth lighting: interpolate sun/block light across face
      vertices (average the 4 cells around each corner) instead of flat
      per-face. Same ordering constraint as AO.
- [ ] (new) Distance fog matched to render distance (few shader lines;
      hides chunk pop-in; fog color follows the Batch H day/night sky).

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

Addenda first (small: A's save safety + seed file, B's registry, C's bench/
golden screenshot, D's AO/smooth light/fog), then F → G → H. The A–C addenda
are independent and can be batched together; the D addenda must precede F's
greedy mesher because merging rules depend on corner AO/light; entities live
inside G because dropped items are the gentle on-ramp to mobs; polish last.
