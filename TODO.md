# TODO

Developer-facing checklist distilled from the old detailed roadmap. Keep the
friendly product view in `ROADMAP.md`; keep concrete planning, implementation
notes, and verification follow-through here or in `docs/handoff/`.

## Completed

### Usable game foundation

- [x] On-screen debug overlay.
- [x] Hotbar UI.
- [x] Player persistence.
- [x] `settings.cfg`.
- [x] Versioned save headers.
- [x] World metadata file: `saves/world1/level.bin` stores magic, version, and
  seed so worlds do not change if the default seed constant changes later.
- [x] Atomic save writes for chunk, player, and level files through temporary
  files followed by rename.
- [x] Autosave timer and save-on-unload coverage.

### Better terrain

- [x] Trees.
- [x] Wood, Leaves, Sand, and Bedrock blocks.
- [x] Two-scale terrain with plains and hills.
- [x] Sand basins.
- [x] Bedrock floor.
- [x] Central block registry table with saved block ids kept append-only.

### Scalable world

- [x] Frustum culling.
- [x] Background chunk generation.
- [x] Background mesh building.
- [x] Performance counters in the debug overlay.
- [x] `--bench N` for repeatable performance checks.
- [x] Golden screenshot regression test from a fixed viewpoint.

### Lighting and visual shading

- [x] Sun light and block light per cell.
- [x] Relight on edits.
- [x] Light propagation across chunk borders.
- [x] Light baked into mesh vertices.
- [x] Torch block.
- [x] Per-vertex ambient occlusion.
- [x] Smooth lighting around corners without leaking through blocked edges.
- [x] Distance fog tied to render distance.

### Underground and richer terrain

- [x] Caves.
- [x] Coal and iron ore veins.
- [x] Lake basins.
- [x] Water rendering.
- [x] Sunlight attenuation through water.
- [x] Swim physics.
- [x] Skip taller world for now to avoid breaking existing modified chunks.

### Mesh and render optimization

- [x] Greedy meshing for same-block faces that share matching lighting data.
- [x] Texture array for chunk rendering.
- [x] Packed chunk vertices.
- [x] Frame-budgeted mesh upload queue.
- [x] Priority ordering for visible and nearby mesh uploads.
- [x] Front-to-back opaque draw sorting.
- [x] Back-to-front water draw sorting.
- [x] Measure buffer lifecycle cost before adding buffer pooling.
- [x] Skip occlusion heuristics until draw calls are an actual bottleneck.

### Items, inventory, and entity foundation

- [x] Fixed 20 TPS simulation tick.
- [x] Interpolated rendering between ticks.
- [x] Shared AABB body physics for player and entities.
- [x] Minimal item entity system.
- [x] Dropped block items in survival mode.
- [x] Item pickup magnet behavior.
- [x] Item counts in hotbar slots.
- [x] Finite placement in survival mode.
- [x] Inventory grid UI.
- [x] Player inventory persistence and migration from older player saves.

### Polish and distribution

- [x] Break, place, and footstep audio.
- [x] Optional audio build path.
- [x] Pause menu.
- [x] Live settings page.
- [x] Key rebinding through `settings.cfg`.
- [x] Day/night cycle.
- [x] Separate sun and block light channels for night rendering.
- [x] Day clock persistence.
- [x] Install/package target.
- [x] README quickstart.
- [x] Clean-container build verification.

### Survival progression

- [x] Survival mode default for fresh settings.
- [x] Creative/survival toggle with `M`.
- [x] Item registry and item-stack player saves.
- [x] Mining metadata: hardness, preferred tool, harvest tier, drops, and
  durability.
- [x] Timed survival mining with crack overlay.
- [x] Cobblestone, Planks, Crafting Table, Furnace, Diamond Ore, Raw Iron,
  Iron Ingot, Diamond, and tool tiers.
- [x] 2x2 inventory crafting.
- [x] 3x3 crafting table UI.
- [x] Furnace input, fuel, and output UI.
- [x] Furnace block entity persistence.
- [x] Procedural block, item, and crack art.
- [x] 9-column inventory and ninth hotbar slot.
- [x] Player save migration to 36 inventory slots.
- [x] Minecraft-style beveled inventory panels.
- [x] Inventory UI extraction into reusable drawing helpers.
- [x] Demo flags that do not write to real saves.
- [x] Flowing water.
- [x] Shore-exit swimming fix.

### Dropped items and simple creatures

- [x] Choose the item-entity save format and versioning rules.
- [x] Persist dropped item position, velocity, item id, count, age, spin seed,
  and durability data.
- [x] Load item entities when chunks become active.
- [x] Save item entities when chunks unload and during autosave.
- [x] Define cleanup or despawn rules so worlds do not grow unbounded.
- [x] Test quit/load, chunk unload/load, corrupted data, old-version data,
  autosave, and demo save isolation.
- [x] Define the minimal living-entity data model.
- [x] Add health and damage rules.
- [x] Add simple spawning rules for normal worlds using deterministic ambient
  chunk rules.
- [x] Add basic movement, wandering behavior, and movement-facing.
- [x] Add drops that reuse the item entity path.
- [x] Add rendering through runtime authored GLB assets and manifest ids.
- [x] Test ticking, damage, drops, chunk streaming, runtime-only save
  interactions, and demo save isolation.

## Backlog

### Expand crafting and recipes

- [ ] Decide which recipe data can move out of C++ without causing churn.
- [ ] Add data-file loading for stable recipe definitions.
- [ ] Preserve deterministic tests for recipe matching and consumption.
- [ ] Expand recipes only where progression needs them.
- [ ] Add recipe browsing, unlocks, or search if the UI needs it.

### Add more world variety

- [ ] Pick one or two concrete sources of variety before widening terrain
  architecture.
- [ ] Preserve generation as a pure function of seed and world coordinates.
- [ ] Add tests for cross-chunk determinism.
- [ ] Verify edits and old saves are not invalidated by generation changes.
- [ ] Avoid committing to biomes, weather, or taller worlds without a separate
  plan.

### Add structures

- [ ] Define structure placement rules.
- [ ] Keep placement deterministic from seed and coordinates.
- [ ] Decide how structures interact with player edits and saved chunks.
- [ ] Add small rewards or useful contents.
- [ ] Test cross-chunk placement, chunk load order, and save/edit behavior.

### Add chests and containers

- [ ] Define container block state and inventory storage.
- [ ] Persist container contents safely.
- [ ] Add chest UI using the existing HUD/inventory helpers.
- [ ] Decide drop behavior when a container is broken.
- [ ] Test save/load, chunk streaming, UI moves, and broken-container drops.

### Introduce general block state

- [ ] Identify the concrete gameplay need before generalizing.
- [ ] Design saveable block state with migration rules.
- [ ] Make state available to meshing, interaction, raycast, and block updates.
- [ ] Keep saved block ids append-only.
- [ ] Test old saves, new saves, mesh updates, and chunk-border behavior.

### Improve interaction feel

- [ ] Tune breaking feedback.
- [ ] Tune placement feedback.
- [ ] Tune held-item animation and mining swing.
- [ ] Tune swimming and shore interactions.
- [ ] Improve selection clarity.
- [ ] Add or adjust sounds where feedback is weak.
- [ ] Verify feel changes visually and with targeted tests where possible.

### Add world selection

- [ ] Replace the fixed `saves/world1` assumption with named worlds.
- [ ] Define world metadata shown in menus.
- [ ] Add create, load, and delete or archive flows.
- [ ] Protect real saves from accidental deletion.
- [ ] Migrate the existing default world path safely.
- [ ] Test world creation, bad metadata, load selection, and save isolation.

### Clean up render distance

- [ ] Decide whether to cap render distance, raise the far plane, or separate
  load distance from draw distance.
- [ ] Make fog, projection, and visible terrain agree.
- [ ] Verify the change with screenshots at high render distance.
- [ ] Benchmark memory, streaming, and frame time before and after.

### Move stable data out of C++

- [ ] Start with low-risk data such as recipes or tuning tables.
- [ ] Add versioning and validation for data files.
- [ ] Keep bad data failures clear and non-destructive.
- [ ] Avoid full modding, scripting, and custom block formats until the core
  rules settle.

## Later

- [ ] Multiplayer: requires world authority, chunk delta sync, entity
  replication, prediction, reconciliation, save ownership, and protocol design.
- [ ] Real shadow mapping.
- [ ] Full weather.
- [ ] Complex biomes.
- [ ] Taller or infinite vertical worlds.
