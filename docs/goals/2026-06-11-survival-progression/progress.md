# Progress: Batch I Survival Progression

## STATUS

- Current: M5 complete (visual polish + held item, user request 2026-06-12)
- Last verified: 2026-06-12 — warning-free build, `world_tests` all pass,
  ctest 2/2 (golden skipped on Windows), screenshots inspected:
  `--demo-survival` (new icons, lit furnace, papercraft crafting table,
  held pickaxe in MC pose), `--demo-inv`, `--demo-items` (upright cut-out
  drops), `--demo-break` (pixel crack stages), plain fresh run (arm).
- Minor issues parked: 8 (added: golden reference must be regenerated on
  Linux — the HUD hotbar icons changed by design, the Windows ctest run
  skips the golden test so it could not be updated here)
- Blockers: none

## Log

- [2026-06-11] User approved the Batch I direction through the grill-me design
  pass: Minecraft-like survival progression; item registry; crafting and
  furnace included; survival default; mode toggle `M`; strict harvest rules;
  tool tiers wood/stone/iron/diamond; pickaxe/axe/shovel; procedural icons and
  crack overlay; persistent furnace block entities; item entity merging; docs
  and screenshot verification required.
- [2026-06-11] Wrote `goal.md`, `spec.md`, and `plan.md`. Native goal
  registration deliberately held until after spec/plan review per user
  request.
- [2026-06-11] Spec/plan review round 1: REJECTED. Blocking findings:
  mining rules lacked exact numeric formula/table; recipes lacked shapes and
  counts; furnace timing/persistence was underdefined; axe/shovel roles were
  weak; inventory click and item-merge behavior were not verifiable; plan file
  ownership used open-ended paths. Fixed in one batch by adding exact hardness,
  tick, recipe, furnace, click, merge, and file-ownership contracts.
- [2026-06-11] Spec/plan re-review round 2: REJECTED. Remaining findings:
  furnace output-blocked behavior conflicted between spec and plan, and
  shift-click quick-move needed to be explicitly recorded as approved scope.
  Fixed by making lit furnaces consume burn ticks every furnace tick while cook
  progress only advances when smelting can proceed, and by recording
  right-click plus shift-click inventory behavior in `goal.md`.
- [2026-06-11] Spec/plan re-review round 3: APPROVED. Prior issues
  GDD-SPEC-003 and GDD-SPEC-005 resolved; no findings. Native goal registered
  from the approved stopping condition.
- [2026-06-11] M1/T1 DONE. Added item registry, item-based stacks with
  durability, item-aware inventory stacking, block-item mappings for current
  gameplay, and stack-based item entities. Validation: `cmake --build build -j`
  exit 0; `.\build\world_tests.exe` exit 0 and printed `all tests passed`.
- [2026-06-11] M1/T2 DONE. Player saves now use v3 item-stack slots
  (`u16 item`, `u8 count`, `u16 durability`), v1 still migrates to empty
  inventory, and v2 block-stack saves migrate slot-for-slot with Stone ->
  Cobblestone item. Validation: `cmake --build build -j &&
  .\build\world_tests.exe` exit 0; tests printed `all tests passed`.
- [2026-06-11] M1 spec-compliance review round 1: REJECTED. Finding
  M1-SPEC-001: `stacksCompatible` rejected matching durable stacks, contrary
  to the Task 1 helper contract. Fixed helper to treat same item + same
  durability as compatible while stack max 1 still prevents tool merging.
  Validation rerun: `cmake --build build -j && .\build\world_tests.exe` exit
  0; tests printed `all tests passed`.
- [2026-06-11] M1 spec-compliance re-review round 2: APPROVED. Prior issue
  M1-SPEC-001 resolved; no findings.
- [2026-06-11] M1 code-quality review round 1: REJECTED. Important findings:
  runtime stack ingress could store invalid ids/overflowed counts, and v2
  migration bypassed stack sanitization. Minor findings: item ids needed an
  explicit append-only save-format warning and item-stack save encoding should
  be reusable before furnace persistence. Fixed by adding canonical
  `makeItemStack`/`sanitizeLoadedItemStack`/`normalizeItemStack`, reusable
  `ItemSave.h` read/write helpers, invalid/oversized ingress tests, v2 count
  clamping, and explicit item enum ordinals. Validation rerun:
  `cmake --build build -j && .\build\world_tests.exe` exit 0; build was
  warning-free and tests printed `all tests passed`.
- [2026-06-11] M1 code-quality re-review round 2: APPROVED. All prior
  quality findings resolved; M1 milestone gate cleared.
- [2026-06-11] M2/T3 DONE. Appended Cobblestone, Planks, Crafting Table,
  Furnace, and Diamond Ore block ids; added block tool/tier/drop metadata,
  procedural tiles, item mappings, and deterministic deep diamond ore
  generation. Validation: `cmake --build build -j` exit 0 warning-free;
  `.\build\world_tests.exe` exit 0 and printed `all tests passed`.
- [2026-06-11] M2/T4 DONE. Added pure mining rules for hardness, tool class,
  harvest tier, exact tick rounding, useful/wrong drops, progress reset, and
  mining durability use. Survival breaking now advances on fixed 20 TPS held
  input, creative remains instant, and block drops use item-stack harvest
  results. Validation: `cmake --build build -j` exit 0 warning-free;
  `.\build\world_tests.exe` exit 0 and printed `all tests passed`.
- [2026-06-11] M2/T5 DONE. Item entities now merge nearby compatible stacks
  in loaded chunks with earlier-entity absorption, preserve durability through
  pickup, leave inventory-full remainders in-world, and never merge tools.
  Non-block item drops render as procedural icon billboards while block items
  remain cube drops. Validation: `cmake --build build -j` exit 0 warning-free;
  `.\build\world_tests.exe` exit 0 and printed `all tests passed`. Visual:
  isolated temp-dir `.\build\groundwork.exe --demo-items --frames 120` exit 0;
  screenshot inspected with block cubes plus coal/tool billboards visible.
- [2026-06-11] M2 spec-compliance review round 1: REJECTED. Important
  findings: M2-SPEC-001 missing exact spec §3 block-table test coverage, and
  M2-SPEC-002 missing coal ore plus furnace harvest tests. Fixed by adding
  row-by-row harvest table assertions and explicit Coal Ore/Furnace mining
  tick/drop assertions. Validation rerun: `cmake --build build -j` exit 0
  warning-free; `.\build\world_tests.exe` exit 0 and printed `all tests
  passed`.
- [2026-06-11] M2 spec-compliance re-review round 2: APPROVED. Prior findings
  M2-SPEC-001 and M2-SPEC-002 resolved; no new Critical or Important
  spec-compliance findings.
- [2026-06-11] M2 code-quality review round 1: APPROVED. No Critical or
  Important findings. Parked minor issues for later cleanup: legacy
  `BlockDef::drop` column remains after item-drop migration; `Entities`
  item-stack ingress should use a stricter canonical split/normalization
  helper before more producers arrive; block-item reverse mapping duplicates
  `placeBlock` metadata and should eventually derive or be more generically
  guarded. M2 milestone gate cleared.
- [2026-06-11] M3/T6 DONE. Added pure data-shaped crafting recipes for 2x2
  and 3x3 surfaces, including logs to planks, sticks, crafting table, torches,
  furnace, and wood/stone/iron/diamond pickaxe/axe/shovel recipes. Crafting
  matching, output construction, full-durability tool output, cursor merge
  checks, and exact ingredient consumption are covered headlessly. Validation:
  `cmake --build build -j` exit 0 warning-free; `.\build\world_tests.exe` exit
  0 and printed `all tests passed`.
- [2026-06-11] M3/T7 DONE. Added `BlockEntityStore` with furnace state keyed
  by world position, exact Raw Iron -> Iron Ingot smelting, coal fuel
  accounting, output-blocked/missing-input burn behavior, `MCBE` v1
  `block_entities.bin` save/load through atomic save helpers, and World
  ownership for load/save/tick/removal on furnace block removal. Validation:
  `cmake --build build -j` exit 0 warning-free; `.\build\world_tests.exe` exit
  0 and printed `all tests passed`.
- [2026-06-11] M3/T8 DONE. Furnace breaking now separates useful block drops
  from contents: correct pickaxe harvest can drop the Furnace item, wrong/no
  pickaxe drops no Furnace item, and both paths can extract input/fuel/output
  stacks from world-owned furnace state for item-entity spawning. Validation:
  `cmake --build build -j` exit 0 warning-free; `.\build\world_tests.exe` exit
  0 and printed `all tests passed`.
- [2026-06-11] M3 spec-compliance review round 1: REJECTED. Important finding
  M3-SPEC-001: unlit furnaces consumed fresh fuel even when smelting could not
  proceed. Minor finding M3-SPEC-002: bad block-entity load rejected the file
  but did not clear a non-empty target store. Fixed by consuming fresh fuel
  only when smelting can proceed, preserving already-lit burn countdown while
  blocked/missing input, and clearing the output store on bad/malformed loads.
  Validation rerun: `cmake --build build -j` exit 0 warning-free;
  `.\build\world_tests.exe` exit 0 and printed `all tests passed`.
- [2026-06-11] M3 spec-compliance re-review round 2: APPROVED. Prior findings
  M3-SPEC-001 and M3-SPEC-002 resolved; no new Critical or Important
  spec-compliance findings.
- [2026-06-11] M3 code-quality review round 1: APPROVED. No Critical or
  Important findings. Parked minor issues: `recipeAt()` silently clamps
  out-of-range indices instead of exposing caller bugs; M4 furnace UI should
  use a checked World-level furnace accessor to avoid accidentally creating
  persistent orphan block entities. M3 milestone gate cleared.
- [2026-06-11] M4/T9 DONE. Inventory UI now has a reusable slot abstraction
  for inventory, crafting, craft output, and furnace slots; left/right click,
  half-stack splitting, output taking, shift-click crafting, and furnace
  quick-move are covered by headless helper tests. Survival players can open
  the 2x2 inventory craft surface, right-click crafting tables for 3x3
  recipes, right-click furnaces for input/fuel/output management, see recipe
  reference icons, and see item icons plus tool durability bars. Validation:
  `cmake --build build -j` exit 0 warning-free/no work;
  `.\build\world_tests.exe` exit 0 and printed `all tests passed`; isolated
  temp-dir `.\build\groundwork.exe --demo-inv --frames 120` exit 0 and its
  screenshot was inspected with inventory, crafting grid, recipe reference,
  item counts, and durability bars visible.
- [2026-06-11] M4/T10 DONE. Added a self-contained break overlay renderer,
  pure crack-stage/face-selection helpers, generated RGBA crack stages, and
  `--demo-break` staging for screenshot checks. The overlay draws only the
  current raycast face from break progress, without rebuilding chunk meshes;
  existing procedural item icons remain the inventory and item-entity path.
  Validation: `cmake --build build -j` exit 0 warning-free;
  `.\build\world_tests.exe` exit 0 and printed `all tests passed`; isolated
  temp-dir `.\build\groundwork.exe --demo-break --frames 120` exit 0 and its
  screenshot was inspected with the staged Diamond Ore block and visible
  mid-progress cracks on the targeted face.
- [2026-06-11] M4/T11 DONE. Fresh settings now default to survival while old
  explicit `survival=0` settings remain creative; `key_mode_toggle` defaults
  to `M`, round-trips through settings, and toggles survival/creative at
  runtime while saving `settings.cfg`. Added `--demo-survival` for final
  screenshot staging, and updated README, ROADMAP, STATUS, and VERIFICATION
  docs for the Batch I controls, loop, saves, demo flags, and next roadmap
  pointer. Validation: `cmake --build build -j` exit 0 warning-free;
  `.\build\world_tests.exe` exit 0 and printed `all tests passed`; isolated
  temp-dir `.\build\groundwork.exe --demo-survival --frames 300` exit 0 and
  its screenshot was inspected with survival hotbar resources/tools/counts,
  staged Crafting Table and Furnace, targeted Diamond Ore, and visible mining
  cracks.
- [2026-06-11] M4 spec-compliance review round 1: REJECTED. Important
  findings: M4-T9-001 shift-clicking non-smeltable inventory items could place
  inert junk into the furnace input slot, and M4-T9-002 Task 9 lacked headless
  coverage for furnace slot and recipe-reference hit testing. Fixed by adding
  canonical `isFurnaceSmeltableInput`, routing direct and shift-click furnace
  input policy through it, making furnace shift-click no-op for non-fuel and
  non-smeltable items, moving craft/furnace/recipe panel rect and hit-test
  helpers into `MenuUi.h`, and adding tests for Raw Iron input, Coal fuel,
  junk no-op, furnace slot hit testing, and recipe-reference slot hit testing.
  Validation rerun: `cmake --build build -j` exit 0 warning-free;
  `.\build\world_tests.exe` exit 0 and printed `all tests passed`.
- [2026-06-11] M4 spec-compliance re-review round 2: APPROVED. Prior findings
  M4-T9-001 and M4-T9-002 resolved; no fix-induced regressions found.
- [2026-06-11] M4 code-quality review round 1: REJECTED. Important finding
  M4-REV-001: `savePlayer()` persisted only `app.inv`, so autosave/shutdown
  could lose stacks currently carried by the cursor or held in the transient
  crafting grid. Fixed by adding a pure transient-stack save snapshot helper,
  merging cursor and crafting-grid stacks into the saved inventory copy, and
  testing normal merge plus overflow reporting. Validation rerun:
  `cmake --build build -j` exit 0 warning-free; `.\build\world_tests.exe`
  exit 0 and printed `all tests passed`. Parked minor issues: a checked
  World-level furnace-open accessor would reduce future orphan-creation risk;
  `BreakOverlay::draw()` does not restore the previous blend function; the
  inventory/crafting/furnace drawing in `main.cpp` should be decomposed before
  more UI screens are added.
- [2026-06-11] M4 code-quality re-review round 2: REJECTED. M4-REV-001 was
  only partially resolved because a full inventory plus transient cursor/grid
  stack still wrote a lossy `player.bin` after printing a warning. Fixed by
  making `savePlayer()` fail closed: if transient stacks cannot fit in the
  save snapshot, it skips writing `player.bin` so the last non-lossy player
  save remains intact. Added a regression test proving a failed transient
  snapshot does not overwrite an existing player save. Validation rerun:
  `cmake --build build -j` exit 0 warning-free; `.\build\world_tests.exe`
  exit 0 and printed `all tests passed`.
- [2026-06-11] M4 code-quality re-review round 3: APPROVED. Prior finding
  M4-REV-001 resolved; no fix-induced regressions found. The reviewer also
  reran `git diff --check 82dd6ec..f9b7e61`, `.\build\world_tests.exe`, and
  `cmake --build build -j`; all passed. M4 milestone gate cleared.
- [2026-06-11] Final whole-implementation review: APPROVED. No Critical or
  Important findings. The reviewer walked every spec section and integration
  area across the full Batch I diff and found the implementation satisfies the
  spec. Parked minor issues for future cleanup: remove legacy `BlockDef::drop`
  duplication; derive block-item reverse mapping from one canonical source;
  centralize item entity stack splitting/normalization; make `recipeAt()`
  expose out-of-range caller bugs; add a checked World-level furnace-open
  accessor; restore prior GL blend function in `BreakOverlay::draw()`; split
  inventory/crafting/furnace drawing and interaction state out of `main.cpp`
  before adding more UI surfaces.
- [2026-06-11] Final validation: PASSED. From committed head,
  `cmake --build build -j` exited 0 with `ninja: no work to do`;
  `.\build\world_tests.exe` exited 0 and printed `all tests passed`; isolated
  temp-dir `.\build\groundwork.exe --demo-survival --frames 300` exited 0,
  wrote `screenshot.ppm`, and the PNG was inspected showing survival hotbar
  resources/tools/counts, staged Crafting Table and Furnace, targeted Diamond
  Ore, and visible mining cracks. The temp render directory was deleted after
  inspection.
- [2026-06-12] M5 reopened at user request (same goal/batch): redesign the
  Batch I procedural art and add first-person held-item rendering. Spec/plan
  addendum written; the user supplied Minecraft reference images for tools,
  the arm, the held pose, the crafting table (papercraft), and the lit/unlit
  furnace, and explicitly instructed skipping the spec/plan reviewer.
- [2026-06-12] M5 implementation DONE in one pass:
  - Sprite-map icons (classic MC shapes, transparent backgrounds) for
    stick/coal/raw iron/ingot/diamond and pickaxe/axe/shovel x 4 tier
    palettes; HUD atlas + chunk texture array now RGBA; HUD tile mode and
    the item shader respect alpha (dropped tools are cut-out sprites);
    fixed the upside-down V mapping in the item cube/billboard geometry.
  - Crafting table papercraft faces (waffle-grid top, strap-and-tools
    side); furnace redesigned (vent slot + firebox front shown on all four
    side faces); appended Block::FurnaceLit (id 17, emission 13, fire in
    the firebox) and World::tickBlockEntities now syncs lit/unlit to the
    burn state, keeping the block entity across the swap; staggered
    cobblestone; ore blob edge shading; crack overlay redesigned as
    quantized pixel cracks + crumble speckles.
  - First-person viewmodel pass (ItemRenderer::drawHeld, depth-cleared,
    world-lit): held block as mini cube, tools/materials as cut-out sprite
    in the vanilla 90-degree pose (user-tuned twice against reference
    screenshots), bare arm cuboid when the hand is empty, click swing +
    continuous mining chop animation.
  Validation: warning-free `cmake --build build -j`; `world_tests.exe`
  `all tests passed` (new testFurnaceLitBlockSync; BLOCK_TYPES tripwire
  bumped 17 -> 18 for the appended id); ctest 2/2 (golden skipped on
  Windows, regeneration parked for Linux); demo screenshots inspected.
