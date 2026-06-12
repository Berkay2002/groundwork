# Batch I Survival Progression Implementation Plan

**Goal:** `docs/goals/2026-06-11-survival-progression/goal.md`
**Spec:** `docs/goals/2026-06-11-survival-progression/spec.md`
**Milestones:** M1 item/save foundation, M2 blocks/mining/drops, M3 crafting/furnace logic, M4 UI/rendering/docs
**Process:** TDD per task: write focused tests first, watch them fail, implement, validate, commit, log progress.

## Milestone M1: Item and Save Foundation

**Milestone validation:** `cmake --build build -j && ./build/world_tests` on Linux, or the Windows `.exe` equivalent, both warning-free with all tests passing.
**Review:** batched spec-compliance and code-quality review over the full M1 diff.

### Task 1: Item Registry and ItemStack Model          [Milestone M1]

**Files:** create `src/sim/Item.h`; modify `src/sim/Inventory.h`, `src/sim/PlayerSave.h`, `src/sim/Entity.h`, `src/sim/Entity.cpp`, `src/render/ItemRenderer.cpp`, `src/app/main.cpp`, `src/ui/MenuUi.h`, `tests/test_world.cpp`.

**Behavior:**
- Introduce an append-only item identity separate from `Block` ids.
- Define item metadata for placeable block items, materials, fuel, and wood/stone/iron/diamond pickaxe/axe/shovel tools.
- `ItemStack` stores item id, count, and durability. Tools stack to 1 and carry durability; normal items stack to 64 unless item metadata says otherwise.
- Inventory add/consume/merge behavior uses item metadata stack limits, not a hardcoded block stack maximum.
- Empty hand remains implicit and is not an inventory item.
- Existing block-placement callers can still determine the placeable block from the selected survival stack.
- Add a helper for constructing full-durability tool stacks and a helper for durable-item compatibility. These helpers are the future extension point for weapons, armor, and other durable items.

**Tests that must pass:**
- Item registry rows have names, valid stack limits, valid block placement mappings, valid tool metadata, and valid fuel metadata.
- Tools are non-stackable and start with full durability when created through helper logic.
- Inventory fills, tops up, overflows, consumes, and rejects incompatible durable stacks correctly.
- Durable-stack compatibility accepts matching item/durability and rejects different durability.

**Validation:** `cmake --build build -j && ./build/world_tests` -> warning-free build and all tests passed.
**Out of scope:** crafting, furnace state, UI redesign, data-file loading.

### Task 2: Player Save Migration to Item Stacks          [Milestone M1]

**Files:** modify `src/sim/PlayerSave.h`, `src/sim/Item.h`, `src/app/main.cpp`, `tests/test_world.cpp`.

**Behavior:**
- Bump player save format to a new version that writes item id, count, and durability for every inventory slot.
- Load old v1 position-only saves unchanged except for empty inventory.
- Migrate v2 block-stack inventory saves into the new item-stack format while preserving slot positions and selected hotbar slot.
- Migration maps old block stacks to corresponding block items, except old Stone stacks become Cobblestone item stacks.
- Existing Coal Ore and Iron Ore block stacks remain ore-block item stacks, not processed materials.
- Bad or unknown item ids load safely as empty slots.
- Fresh survival starts with an empty inventory. Existing valid saves keep their inventory through migration/roundtrip.

**Tests that must pass:**
- New player save roundtrips item ids, counts, durability, selected hotbar slot, position, yaw/pitch, and fly state.
- Old v1 save still migrates to empty item inventory.
- Old v2 block-inventory save migrates with slot positions preserved and Stone -> Cobblestone.
- Unknown item ids are sanitized without crashing.
- Fresh/default player state in survival has all inventory slots empty.

**Validation:** `cmake --build build -j && ./build/world_tests` -> warning-free build and all tests passed.
**Out of scope:** entity persistence, world save slot management.

## Milestone M2: Blocks, Terrain, Mining, and Drops

**Milestone validation:** `cmake --build build -j && ./build/world_tests` warning-free with all tests passing.
**Review:** batched spec-compliance and code-quality review over the full M2 diff.

### Task 3: Block Registry Progression Data and New Blocks          [Milestone M2]

**Files:** modify `src/world/Block.h`, `src/render/Texture.cpp`, `src/world/Terrain.cpp`, `tests/test_world.cpp`.

**Behavior:**
- Append Cobblestone, Planks, Crafting Table, Furnace, and Diamond Ore block ids without renumbering existing blocks.
- Treat existing `Wood` as Log in names/gameplay while preserving its saved id.
- Extend block definitions with preferred tool class, minimum harvest tier, and item drop definitions.
- Apply the exact hardness/tool/drop table from spec §3.
- Add procedural textures for the new blocks.
- Add deterministic rare deep Diamond Ore generation that preserves chunk-order independence.
- Keep coal and iron generation mostly unchanged unless local tests expose a necessary adjustment.

**Tests that must pass:**
- Block registry consistency covers new metadata, append-only ids, the exact spec §3 hardness table, and axe/shovel roles for logs/planks/crafting table and dirt/grass/sand.
- Stone drops Cobblestone item; ores expose the right harvest requirements and drops.
- Diamond Ore generation is deep, rare enough for the local test bounds, and deterministic across chunk load order.
- New block tiles are valid and no texture id maps out of range.

**Validation:** `cmake --build build -j && ./build/world_tests` -> warning-free build and all tests passed.
**Out of scope:** data-file block definitions, silk touch, broader biome/ore rebalance.

### Task 4: Survival Mining Progress, Harvest Rules, and Durability          [Milestone M2]

**Files:** create `src/sim/Mining.h`, `src/sim/Mining.cpp`; modify `CMakeLists.txt`, `src/app/main.cpp`, `src/world/Block.h`, `src/sim/Inventory.h`, `src/sim/Item.h`, `tests/test_world.cpp`.

**Behavior:**
- Survival breaking uses 20 TPS progress, block hardness, preferred tool class, tier speed, harvest tier, and tick rounding.
- `src/sim/Mining.*` owns pure mining helpers for required ticks, harvest result, progress state, and durability use reason `Mining`.
- Creative keeps instant breaking and no drops.
- Progress resets on target change, release, range loss, menu/inventory opening, or mode change.
- Correct harvest produces the block definition's item drop. Wrong tool/tier can break but produces the wrong-harvest result, usually no drop.
- Tools lose 1 durability on successful non-instant block breaks, even when the wrong tool yields no drop. Broken tools disappear.
- Empty hand works as the implicit no-durability mining tool.
- Break/place sounds are still triggered once at the actual block removal/place event, not every progress tick.

**Tests that must pass:**
- Hand/log/soft block, axe/log, shovel/dirt, pickaxe/stone/ore mining times match the exact spec §4 tick examples and formula.
- Stone by hand breaks slowly and drops nothing; wooden pickaxe harvests Cobblestone; stone pickaxe harvests Raw Iron; iron pickaxe harvests Diamond.
- Bedrock never progresses to broken.
- Durability decrements and tool disappearance are covered.
- Progress reset cases are covered by pure mining state tests where possible.
- Break sounds are represented by one block-removal event in the tested mining state, so app integration has a single audio trigger point.

**Validation:** `cmake --build build -j && ./build/world_tests` -> warning-free build and all tests passed.
**Out of scope:** Haste, Mining Fatigue, underwater/airborne penalties, enchantments, combat durability.

### Task 5: Item Entity Stacks, Drops, and Non-Block Render Path          [Milestone M2]

**Files:** modify `src/sim/Entity.h`, `src/sim/Entity.cpp`, `src/render/ItemRenderer.h`, `src/render/ItemRenderer.cpp`, `src/render/Texture.h`, `src/render/Texture.cpp`, `src/app/main.cpp`, `tests/test_world.cpp`.

**Behavior:**
- Item entities carry `ItemStack` instead of block/count.
- Block drops spawn item stacks from block harvest output.
- Nearby compatible item entities merge using the exact spec §8 radius, ordering, loaded-chunk, and compatibility rules. Durable tools do not merge.
- Pickup preserves item durability and leaves any inventory-full remainder in the world.
- Block items render as small cubes. Non-block items render as procedural icon billboards from an item icon atlas or equivalent generated texture.

**Tests that must pass:**
- Drops spawn the expected item stack for harvested blocks.
- Pickup preserves item id, count, and durability.
- Same-item stack entities merge up to max stack count.
- Merge tests assert the 0.75 block radius, earlier-entity absorbs later-entity ordering, and no merge for unloaded/frozen entities.
- Tools and incompatible durability stacks do not merge.
- Full inventory leaves correct remainder.

**Validation:** `cmake --build build -j && ./build/world_tests` -> warning-free build and all tests passed.
**Out of scope:** item entity persistence, cleanup policy beyond current despawn and merging.

## Milestone M3: Crafting and Furnace Logic

**Milestone validation:** `cmake --build build -j && ./build/world_tests` warning-free with all tests passing.
**Review:** batched spec-compliance and code-quality review over the full M3 diff.

### Task 6: Recipe Tables and Crafting Logic          [Milestone M3]

**Files:** create `src/sim/Crafting.h`, `src/sim/Crafting.cpp`; modify `CMakeLists.txt`, `tests/test_world.cpp`.

**Behavior:**
- Add data-shaped hardcoded shaped recipe tables for 2x2 and 3x3 crafting.
- Implement the exact spec §5 recipe shapes and output counts.
- Implement recipe matching independent of UI.
- Implement crafting consumption and output production for the required Batch I recipes.
- Tool recipes create full-durability tools.
- Recipe reference data can be enumerated for UI display later.

**Tests that must pass:**
- Log -> Planks x4, Planks -> Sticks x4, Planks -> Crafting Table x1, Coal + Stick -> Torches x4.
- 3x3 recipes for Furnace x1 and all approved pickaxe/axe/shovel tiers with full durability.
- Wrong shapes and missing ingredients do not match.
- Crafting consumes exactly the required inputs and produces correct count/durability.

**Validation:** `cmake --build build -j && ./build/world_tests` -> warning-free build and all tests passed.
**Out of scope:** data-file recipes, recipe unlocks/search, drag-crafting.

### Task 7: Furnace State, Smelting, and Persistence          [Milestone M3]

**Files:** create `src/world/BlockEntity.h`, `src/world/BlockEntity.cpp`; modify `src/world/World.h`, `src/world/World.cpp`, `src/platform/SaveIO.h` only if the existing helper needs a small extension, `CMakeLists.txt`, `tests/test_world.cpp`.

**Behavior:**
- Introduce a main-thread block-entity store keyed by world block position for Furnace state.
- Furnace state includes input, fuel, output, cook progress, and remaining burn time.
- Coal provides 1600 burn ticks, Raw Iron smelts in 200 ticks, and furnace ticking follows spec §6 exactly.
- Raw Iron smelts into Iron Ingot when output space allows.
- Furnace state saves/loads separately from raw chunk block bytes and rejects bad headers safely.
- Furnace state saves to `saves/world1/block_entities.bin` using magic `MCBE`, version 1, item-stack serialization, int32 positions/counters, and atomic writes.
- When lit, furnace burn time decrements every furnace tick even if output is
  blocked or input is missing. Cook progress advances only while smelting can
  proceed and resets when it cannot.
- Removing a furnace block removes its state.

**Tests that must pass:**
- Coal smelts Raw Iron into Iron Ingot with correct fuel accounting.
- Coal burn and Raw Iron cook timing are asserted at exact tick counts: no output before 200 cook ticks, one coal supports exactly 8 smelts.
- Output-blocked and missing-input tests assert that burn time continues while
  lit, while cook progress stops/resets.
- Furnace state roundtrips through save/load with stacks and progress intact.
- Bad furnace-state saves are rejected safely.
- Removing a furnace clears its state.
- Save/load tests assert the exact path, magic/version behavior, and position-keyed state.

**Validation:** `cmake --build build -j && ./build/world_tests` -> warning-free build and all tests passed.
**Out of scope:** chests, general block state framework, multiple fuel types beyond coal unless trivial through the fuel table.

### Task 8: Furnace Breaking and Content Drops          [Milestone M3]

**Files:** modify `src/world/BlockEntity.h`, `src/world/BlockEntity.cpp`, `src/world/World.h`, `src/world/World.cpp`, `src/sim/Entity.h`, `src/sim/Entity.cpp`, `src/app/main.cpp`, `tests/test_world.cpp`.

**Behavior:**
- Breaking a furnace with the correct harvest rules drops the Furnace item and its contents.
- Breaking a furnace with the wrong tool/tier still drops its contents but not the Furnace item when harvest rules say no useful block drop.
- Furnace UI, if open, closes safely when the targeted/open furnace no longer exists.
- Content drops spawn as item entities with stack counts and durability preserved.
- Crafting Table harvest behavior follows spec §3 and is covered here or in Task 4: axe-preferred, hand-harvestable, drops itself.

**Tests that must pass:**
- Furnace content stacks are dropped on block removal.
- Correct pickaxe harvest drops furnace block item plus contents.
- Wrong/no pickaxe breaks furnace without furnace item but still drops contents.
- State is absent after break.
- Crafting Table breaks/drops according to its axe-preferred hand-harvestable rule.

**Validation:** `cmake --build build -j && ./build/world_tests` -> warning-free build and all tests passed.
**Out of scope:** persistent item entities after restart.

## Milestone M4: UI, Rendering, Demo, and Documentation

**Milestone validation:** `cmake --build build -j && ./build/world_tests` warning-free with all tests passing, plus a survival/demo screenshot rendered and inspected.
**Review:** batched spec-compliance and code-quality review over the full M4 diff.

### Task 9: Inventory, Crafting Table, Furnace, and Recipe UI          [Milestone M4]

**Files:** modify `src/ui/MenuUi.h`, `src/ui/Hud.h`, `src/ui/Hud.cpp`, `src/app/main.cpp`, `src/sim/Crafting.h`, `src/world/BlockEntity.h`, `tests/test_world.cpp`.

**Behavior:**
- Survival inventory opens with player inventory, 2x2 crafting grid, output slot, and read-only recipe reference panel.
- Right-clicking Crafting Table opens 3x3 crafting UI.
- Right-clicking Furnace opens furnace UI with input/fuel/output/progress.
- Interactable block use takes priority over placement unless sneaking.
- Left-click/right-click stack interactions match the exact spec §7 click rules.
- Shift-click quick-move works only for the exact unambiguous destinations in spec §7.
- Tool durability bars render for any durable item.
- Crafting grids return contents to inventory or drop overflow when closed.
- Recipe reference panel is generated from the recipe table, not hardcoded duplicate UI text.

**Tests that must pass:**
- Layout/hit-test helpers identify inventory, crafting, output, furnace, and recipe panel slots.
- Click helpers split/place/merge whole and single items correctly.
- Right-click tests cover ceil-half pickup, one-item placement, compatible merge, incompatible no-op, output-slot click, and furnace slot constraints.
- Closing transient grids preserves or drops contents according to inventory capacity.
- Interact-priority logic can be tested without a GL context where practical.
- Recipe reference layout enumerates the same recipe table used by crafting logic.

**Validation:** `cmake --build build -j && ./build/world_tests` -> warning-free build and all tests passed.
**Out of scope:** full recipe book, search, unlocks, drag-painting, full creative inventory.

### Task 10: Break Crack Overlay and Item/Block Procedural Art          [Milestone M4]

**Files:** create `src/render/BreakOverlay.h`, `src/render/BreakOverlay.cpp`; modify `src/render/Texture.h`, `src/render/Texture.cpp`, `src/app/main.cpp`, `CMakeLists.txt`, `tests/test_world.cpp`.

**Behavior:**
- Generate procedural item icons for materials and tools, plus procedural crack stages.
- Render crack overlay as a separate targeted-face overlay driven by current break progress.
- Do not rebuild chunk meshes for crack progress.
- Non-block item icons render in inventory and dropped-item billboard path.
- New block textures and icons keep the no-asset-files property.

**Tests that must pass:**
- Pure icon/tile/crack id helpers stay in range and deterministic.
- Break stage selection clamps correctly across progress values.
- New procedural art paths do not require external files.

**Validation:** `cmake --build build -j && ./build/world_tests` -> warning-free build and all tests passed.
**Out of scope:** high-fidelity 3D models for tools or materials.

### Task 11: Survival Default, Demo Flags, and Documentation          [Milestone M4]

**Files:** modify `src/platform/Settings.h`, `src/platform/KeyBinds.h`, `src/app/main.cpp`, `README.md`, `ROADMAP.md`, `docs/handoff/STATUS.md`, `docs/handoff/VERIFICATION.md`, `tests/test_world.cpp`.

**Behavior:**
- Fresh settings default to survival mode.
- Add rebindable `key_mode_toggle` defaulting to `M`; runtime toggle saves settings and switches between survival and creative behavior.
- Creative remains a fixed infinite block palette with instant break/no drops.
- Add one or more demo flags, including `--demo-survival`, to stage Batch I UI/progression visuals for `--frames` screenshot verification.
- Update README, roadmap, and handoff status with controls, progression, implementation notes, and verification evidence.
- Preserve old settings files: an explicitly written `survival=0` keeps creative on load.

**Tests that must pass:**
- Settings parse/write roundtrip includes survival default and mode toggle.
- Mode toggle behavior preserves creative/survival distinctions in focused helpers where practical.
- Demo setup is deterministic enough for screenshot verification.
- Fresh missing settings produce survival enabled; explicit `survival=0` stays creative.

**Validation:** `cmake --build build -j && ./build/world_tests` -> warning-free build and all tests passed; `./build/groundwork --demo-survival --frames 300` or equivalent writes `screenshot.ppm` and the resulting PNG is visually inspected.
**Out of scope:** home screen, save slots/world creation, full creative inventory.

## Final Verification

Before reporting completion:
- Run every validation command from `goal.md` fresh.
- Run `ctest --test-dir build` if available and report whether the golden screenshot ran, skipped, or failed.
- Inspect the survival/demo screenshot and include the path in the final report.
- Walk the spec requirement by requirement with evidence.
- Confirm no asset files were introduced.
- Confirm `ROADMAP.md`, `README.md`, and `docs/handoff/STATUS.md` are updated.

## Milestone M5: Visual Polish and Held Item (2026-06-12 addendum)

**Milestone validation:** `cmake --build build -j` warning-free,
`world_tests` all pass, and the four spec 14.5 screenshots rendered from a
temp dir and visually inspected.

### Task 12: Sprite Icon Art, RGBA Atlas, and Alpha-Aware Item Rendering   [Milestone M5]

**Files:** modify `src/render/Texture.cpp`, `src/ui/Hud.cpp`,
`src/render/ItemRenderer.cpp`, `tests/test_world.cpp` (if icon helpers gain
testable surface).

**Behavior:** spec 14.1 — sprite-map icons for items and tools, RGBA
atlas/texture array, HUD alpha multiply, item-shader alpha discard, torch
icon background transparency, dropped-item V-orientation fix.

**Tests that must pass:** existing icon-mapping and tile-range tests still
pass; build stays warning-free.
**Validation:** build + tests; `--demo-inv` and `--demo-items` screenshots
inspected.

### Task 13: Workstation, Cobblestone, Ore, and Furnace Face Art          [Milestone M5]

**Files:** modify `src/render/Texture.cpp`, `src/world/Block.h`.

**Behavior:** spec 14.2 — crafting table top/side, furnace side/front art,
furnace front mapped onto the four side faces, staggered cobblestone, ore
edge shading.

**Tests that must pass:** block registry/tile tests still pass.
**Validation:** build + tests; `--demo-survival` screenshot shows the
crafting table and furnace front.

### Task 14: Break-Crack Overlay Redesign          [Milestone M5]

**Files:** modify `src/render/Texture.cpp` (crack stage art only).

**Behavior:** spec 14.3 — pixelated crack + crumble stages, quantized alpha.
**Tests that must pass:** crack-stage helper tests unchanged and passing.
**Validation:** build + tests; `--demo-break` screenshot inspected.

### Task 15: First-Person Held-Item Pass with Swing          [Milestone M5]

**Files:** modify `src/render/ItemRenderer.h`, `src/render/ItemRenderer.cpp`,
`src/app/main.cpp`; new pure swing-curve helper header if needed;
`tests/test_world.cpp` for pure helpers.

**Behavior:** spec 14.4 — viewmodel pass for the selected stack (cube for
block items, sprite for tools/materials), own projection, depth-safe,
world-lit, swing while mining and on click.

**Tests that must pass:** swing/transform pure helpers (if extracted) are
deterministic and bounded; existing tests pass.
**Validation:** build + tests; `--demo-survival` screenshot shows the tool
in hand.

### Task 16: Golden Reference, Docs, and M5 Verification          [Milestone M5]

**Files:** `tests/golden/reference.png` (regenerate), `docs/handoff/STATUS.md`,
`README.md`/`ROADMAP.md` if user-facing behavior notes apply.

**Behavior:** regenerate golden only after inspecting the new visuals;
update docs; run the full spec 14.5 verification fresh.
**Validation:** all goal.md validation commands fresh; `ctest --test-dir
build` result reported.
