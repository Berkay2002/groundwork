# Roadmap (v3)

Same F → G → H spine; the groundwork items were filed as addenda under the
done batch they conceptually extend (persistence under A, blocks under B,
perf/verification under C, lighting visuals under D) rather than as new
batches. **All A–D addenda are now done** (2026-06-11). Batch E reflects
what was actually built. Batches A–H are done; future batches start at I and
focus on turning the playable voxel base into a game loop. Future entries are
goals, not task lists. When a batch is approved, write the concrete plan from
the current code and the current model/tooling context.

## Batch A — Usable Game Foundation — DONE (incl. addenda)

- On-screen debug overlay, hotbar UI, player persistence, `settings.cfg`,
      versioned save headers
- World metadata file (`saves/world1/level.bin`: `MCLV` + version +
      seed). Written on world creation, read back on load, so the save stays
      valid even if the default seed constant in `main.cpp` changes; bad/old
      files are rewritten with the fallback seed.
- Atomic save writes: chunk/player/level files stream to `<name>.tmp`
      and `rename()` over the old file (`src/SaveIO.h`), so a crash
      mid-write can't corrupt real player data.
- Autosave + save-on-unload audit: chunks streaming out were already
      saved (now covered by a test); a 30 s timer in the main loop also
      saves modified chunks + player, so a crash loses at most ~30 s.

## Batch B — Better Terrain — DONE (incl. addendum)

- Trees, Wood/Leaves/Sand/Bedrock blocks, two-scale terrain, bedrock floor
- Block registry table: constexpr `BLOCK_DEFS` in `Block.h` (name,
      solid/collidable/opaque/`dimsSunlight`, emission, hardness, drop,
      per-face tiles) replacing the predicate/`tileFor` switches. Hardness
      and drop are recorded for Batch G but unused until then; chunk loading
      clamps unknown saved ids to Air. File-loaded definitions remain the
      later modding step. Enum values stay the saved bytes: **append, never
      renumber** still applies.

## Batch C — Scalable World — DONE (incl. addenda)

- Frustum culling, background gen + meshing on worker threads, perf
      counters in overlay
- `--bench N` flag: run N frames with vsync forced off, print fps,
      drawn/loaded chunk counts, upload totals and gen/mesh worker times,
      exit — the before/after number for Batch F.
- Golden-screenshot regression test: `tests/golden_screenshot.py` runs
      `--frames 400` from a fixed viewpoint in a temp world and compares
      against `tests/golden/reference.png` with a tolerance (overlay text
      noise ~0.03%, limit 1.5%). Registered in ctest; skips without a
      display. Regenerate after intentional visual changes with `--update`.

## Batch D — Lighting — DONE (incl. addenda)

- 4-bit sun + block light, BFS relight on edits, cross-border
      propagation, light baked into mesh verts, Torch block
- Per-vertex ambient occlusion (classic 3-neighbor corner darkening in
      `buildMeshData`; `ChunkSnapshot` gained diagonal corner columns; quad
      split follows the darker diagonal). Landed before greedy meshing as
      required — face merging must now compare corner AO/light.
- Smooth lighting: vertex brightness averages the open cells around
      each corner (light can't leak around an edge: the diagonal cell is
      skipped when both sides are opaque). Water keeps flat per-face light
      so a lake stays one even sheet.
- Distance fog matched to render distance — was already in the chunk
      shader since the MVP (0.7–0.98 of render distance, fading to the
      `uSky` uniform, which the Batch H day/night sky will drive); verified
      and ticked rather than rebuilt.

## Batch E — Underground & Richer Terrain — DONE

- Spaghetti caves (two 3D fBm fields both near zero, surface-pinched
      taper, bedrock intact, lake/shore columns protected, no floating trees)
- Ores (`CoalOre = 9`, `IronOre = 10`): per-8³-cell hashed vein
      candidates → clumpy blobs replacing stone only, depth-banded
      (coal ≤ y 44, iron ≤ y 22), chunk-aligned so veins never cross borders
- Water (`Water = 11`): lake basins sunk to SEA_LEVEL 20 via a third
      height mask, translucent second mesh pass per chunk
      (`waterVerts`/`drawWater`, blend on, cull off, depth writes on),
      `dimsSunlight` attenuation (~1 level/block of depth), swim physics,
      hotbar slot 8
- ~~Taller world~~ — skipped: caves fit fine in the 80-high world, and a
      CHUNK_HEIGHT bump would regenerate the player's modified chunks

## Batch F — Mesh & Render Optimization — DONE

- Greedy meshing: coplanar same-block faces merge only when their
      quantized corner AO/light tuples are identical (the Batch D
      constraint), for both the opaque and water meshes; torches stay
      custom geometry. Texture wrap solved by moving chunk rendering to a
      GL_TEXTURE_2D_ARRAY (tile = layer, REPEAT wrapping); the HUD keeps
      the 2D strip atlas, both built from the same tile functions.
      ~9.5k → ~1.7k verts/chunk near spawn.
- Vertex size reduction: 24-byte float vertex → 12-byte packed ints
      (u16 chunk-local position and uv in 1/16 units — torch geometry stays
      exact — u8 brightness, u8 layer; the chunk origin became a per-draw
      uniform). Batch H later grew this to 14 bytes (split sun/block light
      channels + pad). Combined with greedy: 222 KB → 20 KB vertex data per chunk;
      steady-state bench 299 → ~340 fps at render distance 6.
- Frame-budgeted main-thread work: finished meshes wait in an upload
      queue and upload within a 3 ms/frame cap (`UPLOAD_BUDGET_MS`),
      in-frustum + nearest chunks first, dropped if their chunk unloaded,
      newest result kept per chunk; dirty-chunk *enqueueing* is also
      priority-ordered now (was hash-map order).
- GL buffer lifecycle: measured first, as specified — a full chunk
      upload costs ~12 µs now that greedy meshes are ~30 KB, so VBO
      pooling/orphaning has nothing left to save. Skipped on the numbers.
- Chunk draws sorted front-to-back for early-z; water back-to-front
      (blend-correct — it was unordered before). Per-chunk VAOs kept
      instead of "one persistent VAO": GL 3.3 has no separate
      attrib-binding state, so one VAO bind per chunk *is* the minimal
      call sequence.
- Occlusion heuristics: bench shows we are nowhere near draw-bound
      (~80 draws/frame at ~340 fps), so skipped per the "don't guess" rule.

## Batch G — Items, Inventory & Entity Foundation — DONE

- (new) Fixed-timestep simulation tick (20 TPS, `TICK_DT` accumulator in
      main.cpp) for player/entity logic, decoupled from render with
      position interpolation (`prevPos` + alpha) — the "don't bake in
      single-player assumptions" insurance for later multiplayer
- (new) Minimal entity system (`src/Entity.{h,cpp}`): position, velocity,
      AABB reusing the player's sub-stepped collision (extracted to
      `src/Physics.{h,cpp}` as `Body`/`moveBody`), update + render lists,
      per-chunk bucketing. Entities freeze in unloaded chunks and are NOT
      persisted across runs (accepted Batch G limitation).
- Block drops: breaking yields the registry's `drop` — dropped items as
      the first entity (small bobbing/spinning cube via
      `src/ItemRenderer.cpp`, magnetized pickup within ~2 blocks). Survival
      mode only; creative keeps destroy-outright.
- Item counts in hotbar slots + finite placement (`survival=1` in
      settings.cfg; creative remains available with `survival=0` or `M`)
- Full inventory grid UI (open/close with E, click to move/swap/merge
      stacks, Hud-based, 4×8 with the hotbar as row 0)
- Inventory persistence in player.bin (save bumped to v2; v1 files
      migrate with an empty inventory instead of being rejected)

## Batch H — Polish & Distribution — DONE

- Audio: break/place/footstep effects synthesized at startup (no asset
      files), mixed by a small voice pool over the vendored miniaudio header;
      `-DENABLE_AUDIO=OFF` compiles it out, a missing device just means
      silence; `volume` in settings.cfg
- Pause menu: Esc pauses the simulation behind a dim overlay
      (resume / settings / quit); the settings page edits render distance,
      FOV, sensitivity, volume, vsync live and writes settings.cfg on every
      change
- Key rebinding via `key_*` entries in settings.cfg (movement, jump,
      sneak, sprint, fly, inventory; names per `KeyBinds.h`)
- Day/night cycle: 10-minute world day drives sky/fog color and a
      sun-level uniform; vertex light carries separate sun/block channels so
      night needs no relighting/remeshing and torches stay bright; day clock
      persisted in level.bin v2 (v1 migrates keeping its seed)
- Release packaging: `cmake --install build --strip --prefix dist`
      (449 KB self-contained binary), README quickstart, verified from
      scratch in a clean ubuntu:24.04 container

## Batch I — Survival Progression — DONE

Survival is now the default for fresh settings, with `M` toggling back to
creative and saving the choice. The loop is Minecraft-like and item-based:
logs -> planks/sticks -> crafting table -> wooden tools -> cobblestone ->
stone tools/furnace -> coal/torches -> raw iron -> iron ingot -> iron tools ->
diamond ore -> diamond tools.

- Item registry + player save v3 item stacks, with old v1/v2 migration.
- Block registry mining metadata: hardness, preferred tool, harvest tier,
      correct/wrong drops, and durability use.
- Timed 20 TPS survival mining with crack overlay; creative remains instant
      destroy/no-drop.
- New blocks/items: cobblestone, planks, crafting table, furnace, diamond ore,
      raw iron, ingots, diamond, and wood/stone/iron/diamond pickaxe/axe/shovel.
- 2x2 inventory crafting, 3x3 crafting table UI, recipe reference icons,
      right-click/shift-click stack behavior, and furnace input/fuel/output UI.
- Furnace block entities persist input/fuel/output and smelt Raw Iron with Coal.
- Procedural block/item/crack art; no asset files.
- Inventory UI addenda (post-M5, same commit spine):
  - 9-column inventory + 9th hotbar slot (was 8); hotbar cycling wraps correctly.
  - Player save v4: 36-slot inventory; v3 migrates in place.
  - MC-style beveled panel rendering (drawPanel helper, dark/light border,
        translucent fill); all inventory/crafting/furnace screens use it.
  - InventoryUi extraction: `src/ui/InventoryUi.{h,cpp}` owns layout,
        drawing, and hit-testing for inventory/crafting/furnace panels;
        `main.cpp` only calls `ui::drawInventoryScreen` and event dispatch.
  - Demo-flag save isolation (spec R6): any `--demo-*` flag sets `demoRun`
        which skips all save I/O on the exit path, the 30 s autosave, and
        the `World` destructor save. `World::setDemoMode()` is the single
        gate; `--demo-craft` opens the 3×3 crafting screen, `--demo-furnace`
        places an active furnace and opens the furnace screen.

## Batch J — Entity Persistence & Item Cleanup

Goal: make item entities durable enough to be a real game system.

Items should survive normal play sessions, behave predictably around chunk
streaming, and have cleanup rules that keep worlds stable. This should happen
before mobs so later entities do not inherit prototype persistence rules.

## Batch K — Small Mob Foundation

Goal: prove that living entities fit the engine.

The batch should establish health, damage, spawning, simple behavior, and drops
without pretending to solve all mob AI. The fixed tick, AABB physics,
per-chunk buckets, and item entities are enough foundation; complex pathfinding
should wait until a concrete need appears.

## Batch L — Advanced Crafting & Recipe Data

Goal: move beyond the base survival recipes without locking the recipe system
into C++ forever.

Batch I added the core tested recipes and crafting surfaces. A later recipe
batch should focus on data-file recipes, unlock/search UX, and recipe expansion
when there are enough blocks, mobs, or structures to justify it.

## Batch M — World Variety Without Overcommitting

Goal: make exploration less repetitive without committing to a new world model.

The world should gain more visual and navigational variety while preserving the
terrain invariant: generation is a pure function of world coordinates and seed.
This is not automatically a biome, weather, or taller-world batch.

## Batch N — Structures

Goal: give exploration authored-feeling rewards without breaking determinism.

The world already has caves, ores, lakes, trees, and water. Structures should
add memorable things to find, with clear placement and save/edit rules, before
the game needs a full authoring pipeline.

## Batch O — Chests & Containers

Goal: let the world hold player-managed inventory.

Containers bridge survival, crafting, structures, and future mobs. The key
design pressure is block-attached state: enough to support containers cleanly
without prematurely designing every stateful block.

## Batch P — Block State System

Goal: introduce block state when the game has earned it.

Block state should solve concrete gameplay needs such as containers,
orientation, variants, doors, crops, furnaces, or stairs. The important
outcome is a saveable, meshable, migratable state model, not a general-purpose
state framework for its own sake.

## Batch Q — Better Interaction Feel

Goal: make core interactions feel deliberate instead of prototype-like.

Breaking, placing, holding, moving, swimming, selection, and feedback should be
clearer and more tactile. Prefer small feel improvements that do not force new
architecture unless the current interaction model is the actual blocker.

## Batch R — Save Slots & World Management

Goal: make worlds explicit, selectable, and safe to manage.

The current save path is fixed as `saves/world1`. That is fine for a demo, but
not once players can invest in more than one world. The priority is reliable
world identity and metadata; richer menus and previews can follow.

## Batch S — Render-Distance & Far-Plane Cleanup

Goal: make render-distance settings truthful.

Current open issue: the projection far plane is fixed at 600, but fog at
render distance 64 spans 717–1004. Distances above roughly 37 chunks cost
streaming/memory without showing all terrain. The batch plan should choose
between capping, dynamic far plane, or separating load/simulation distance from
draw distance, then verify the choice visually and with bench numbers.

## Batch T — Modding & Data Files

Goal: move stable gameplay data out of C++ when stability makes that useful.

Do not start with full modding. Move the lowest-risk data first, only after the
underlying rules have settled enough that the format will not churn every
batch. Full custom blocks and scripting remain later concerns.

## Still Deliberately Later

Multiplayer remains out of scope for now. The fixed 20 TPS tick helps, but
multiplayer would require world authority, chunk delta sync, entity
replication, prediction, reconciliation, save ownership, and protocol design.

Real shadow mapping, full weather, taller/infinite vertical worlds, and complex
biomes are still attractive but not core-loop work. Revisit them after survival
progression, crafting, chests, structures, and simple mobs have proven what
the game needs.
