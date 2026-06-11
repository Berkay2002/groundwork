# Progress: Batch I Survival Progression

## STATUS

- Current: M3 / Task 8
- Last verified: M3/T7 validation green (`cmake --build build -j` exit 0,
  warning-free; `.\build\world_tests.exe` -> `all tests passed`, 2026-06-11)
- Blockers: none
- Minor issues parked: 3

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
