# Minecraft-Style Inventory UI Implementation Plan

**Goal:** docs/goals/2026-06-12-minecraft-inventory-ui/goal.md
**Spec:** docs/goals/2026-06-12-minecraft-inventory-ui/spec.md
**Milestones:** M1 nine columns + save v4, M2 panel layout geometry,
M3 InventoryUi module + visuals
**Process:** TDD per task — tests first, watch them fail, implement,
validate, commit. Build commands (Windows/Ninja per AGENTS.md):
`cmake --build build -j`, `.\build\world_tests.exe`,
`.\build\groundwork.exe <flags>`.

---

## Milestone M1: Nine columns + save v4 (Tasks 1–2)

**Milestone validation:** `cmake --build build -j` exits 0 warning-free;
`.\build\world_tests.exe` prints `all tests passed`.
**Review:** batched spec-compliance + code-quality review over the full
M1 diff.

### Task 1: Inventory 9 columns + hotbar key/palette          [M1]

**Files:** modify `src/sim/Inventory.h`, `src/app/main.cpp`,
`tests/test_world.cpp`

**Behavior:**
- `Inventory::COLS` 8 → 9 (`SLOTS` becomes 36); header comment updated to
  describe 4×9 with slots 0..8 as the hotbar (spec R1).
- `HOTBAR[]` in main.cpp gains `Block::Planks` as the 9th entry;
  `HOTBAR_SLOTS` stays derived from the array, so keys 1–9, scroll-wheel
  wrap, and the in-game hotbar width follow automatically. Verify no other
  code hard-codes 8 columns or 32 slots (search for `8`-column and `32`-slot
  assumptions tied to the inventory).
- Existing inventory/crafting/quick-move tests updated wherever they assume
  32 slots or 8 columns.

**Tests that must pass:**
- `Inventory::SLOTS == 36`; `add` overflow behavior at 36 slots (fill all
  slots, assert leftover returned).
- All existing inventory/crafting/furnace quick-move tests, updated for the
  new slot count, still pass.

**Validation:** `cmake --build build -j && .\build\world_tests.exe` →
exit 0, `all tests passed`
**Out of scope:** save format (Task 2), layout geometry (M2), any drawing.

### Task 2: Player save v4 with v3→v4 migration          [M1]

**Files:** modify `src/sim/PlayerSave.h`, `tests/test_world.cpp`

**Behavior:**
- `PLAYER_VERSION` 3 → 4; v4 writes/reads 36 slots directly (spec R1).
- Version acceptance widens to `{1,2,3,4}`; v4 load reads 36 slots, v3 load
  reads 32 slots placing old slot `r*8+c` at new slot `r*9+c` (column 8 of
  each row left empty), v2 keeps its block-item migration but lands items at
  the `r*9+c` positions, v1 loads an empty inventory. Bad magic / unknown
  version still rejected; short reads still return false (fail-closed).
- The v2 and v3 read paths consume a **fixed literal 32** source slots —
  never `Inventory::SLOTS`, which is 36 after Task 1. Likewise the test
  fixtures that hand-write v2/v3 files must emit a fixed literal 32 slots
  (the existing fixtures loop over `Inventory::SLOTS` and would silently
  write 36-slot "v2/v3" files, masking the migration).
- File header comment updated to describe v4.
- `hotbarSlot` clamping in main.cpp remains correct for 0–8.

**Tests that must pass:**
- v3→v4 migration: hand-write a v3 file (exactly 32 slots, literal count,
  with distinctive contents including a durable item with nonzero wear);
  load; assert each old slot `r*8+c` landed at `r*9+c` with identical
  item/count/durability and column 8 of every row empty.
- v4 round-trip: save a populated 36-slot state, load, assert identical.
- v2 migration test updated: fixture writes exactly 32 block-pairs (literal
  count) and assertions check contents land at `r*9+c` with column 8 empty.
- v1 and bad-header tests unchanged and passing.
- All save tests use temp paths under the existing test-save convention and
  clean up after themselves.

**Validation:** `cmake --build build -j && .\build\world_tests.exe` →
exit 0, `all tests passed`
**Out of scope:** chunk format, real `saves/` directory, layout/drawing.

---

## Milestone M2: Panel layout geometry (Task 3)

**Milestone validation:** `cmake --build build -j` exits 0 warning-free;
`.\build\world_tests.exe` prints `all tests passed` (including the new
layout tests).
**Review:** batched spec-compliance + code-quality review over the full
M2 diff. Single-task milestone: the layout is one coherent pure-geometry
subsystem that M3's renderer consumes, and reviewing it before any drawing
exists keeps the review headless.

### Task 3: Minecraft-panel layout in MenuUi.h          [M2]

**Files:** modify `src/ui/MenuUi.h`, `tests/test_world.cpp`

**Behavior:** (all spec R2; pure, header-only, GL-free)
- New geometry helpers other code depends on (exact names, referenced by
  M3): `ui::Rect panelRect(const InventoryLayout&, InventorySurface, int craftSurface)`,
  `ui::Rect recipePanelRect(const InventoryLayout&)`,
  `ui::Rect playerBoxRect(const InventoryLayout&)`,
  `ui::Rect arrowRect(const InventoryLayout&, InventorySurface, int craftSurface)`.
  `InventoryLayout` may gain fields, but keeps `slot`, `pad`, `x0`, `y0`.
- Contractual signatures unchanged: `inventoryLayout`, `inventorySlotRect`,
  `inventorySlotAt` (both overloads), `craftSlotRect`, `craftOutputRect`,
  `furnaceSlotRect`, `recipeReferenceSlotRect`, `recipeReferenceSlotAt`,
  `uiSlotAt`. Their produced geometry changes to the panel composition:
- Inventory screen (craftSurface 2): panel top section left→right = player
  box (~2 slots wide × ~3.5 slots tall, inside the panel), 2×2 crafting
  grid, arrow, output slot. Below: 3×9 main grid (inventory rows 1–3), gap
  of ~0.45 slot height, 9-slot hotbar row (row 0). Everything inside the
  panel rect, panel centered on screen.
- Crafting-table screen (craftSurface 3): top section = 3×3 grid + arrow +
  output horizontally centered; no player box (playerBoxRect is only
  meaningful for the inventory screen; document this on the function).
- Furnace screen: top section = input slot above fuel slot, room for the
  flame indicator between them, arrow, output slot to the right.
- Recipe panel attached to the right edge of the main panel, recipe slots
  (3 per row, current 30 px size or similar) inside it; crafting surfaces
  only. `uiSlotAt` on the furnace surface keeps returning `none()` for
  recipe positions.
- Slot size 56 px, pad 4 px. At 1280×720 the main panel plus recipe panel
  fit on screen with all rects inside the window.

**Tests that must pass:**
- For each surface (inventory/2×2, crafting-table/3×3, furnace) at
  1280×720: all slot rects pairwise non-overlapping; the center of every
  slot rect maps back to exactly that slot via `uiSlotAt` (recipe slots via
  `uiSlotAt` returning `recipeReference` on crafting surfaces); panel rect
  and recipe panel rect within `[0,1280)×[0,720)`; all slot rects contained
  in the panel (recipe slots in the recipe panel); hotbar row separated
  from the main grid by a vertical gap > 0.
- Furnace surface: points inside where recipe slots would be return
  `UiSlot::none()`.
- Existing click/quick-move logic tests unaffected (geometry-independent).

**Validation:** `cmake --build build -j && .\build\world_tests.exe` →
exit 0, `all tests passed`
**Out of scope:** any drawing or Hud calls; main.cpp changes; visual style.

---

## Milestone M3: InventoryUi module + visuals (Tasks 4–6)

**Milestone validation:** `cmake --build build -j` exits 0 warning-free;
`.\build\world_tests.exe` prints `all tests passed`;
`.\build\groundwork.exe --demo-inv --frames 240`,
`--demo-craft --frames 240`, and `--demo-furnace --frames 240` each exit 0
and write `screenshot.ppm`; the three converted PNGs are visually inspected
against the Minecraft reference layout before review is requested;
`saves/world1` is byte-for-byte untouched by the demo runs (Task 6
save-isolation check).
**Review:** batched spec-compliance + code-quality review over the full
M3 diff, then the final whole-implementation review (Phase 7).

### Task 4: InventoryUi module extraction          [M3]

**Files:** create `src/ui/InventoryUi.h`, `src/ui/InventoryUi.cpp`; modify
`src/app/main.cpp`, `CMakeLists.txt`

**Behavior:** (spec R4; visual style spec R3 lands in Task 5 — this task is
the extraction with current visuals reproduced or near-reproduced)
- New namespace-`ui` module owning screen composition. Public interface
  (M3 tasks depend on these names):
  - `enum class ScreenKind { Inventory, CraftingTable, Furnace }` declared
    in `InventoryUi.h`; main.cpp's file-local `InventoryScreen` enum is
    deleted and its uses replaced by `ui::ScreenKind` (single source of
    truth — the module never sees a main.cpp type).
  - `struct InventoryView` — read-only: `const Inventory&`,
    `const ItemStack& cursorStack`, `const CraftingUiState&`,
    `ScreenKind screen`, `FurnaceState*` (nullable), `float mouseX, mouseY`.
  - `void drawInventoryScreen(Hud&, const InventoryView&, int screenW, int screenH)`
  - `void drawHotbar(Hud&, const HotbarView&, int screenW, int screenH)`
    where `HotbarView` carries: `int hotbarSlot`, `bool survival`,
    `const Inventory&`, the creative palette as `const Block* palette` +
    `int paletteCount`, and `const char* heldName` (the label to display,
    computed by the caller; empty/null = no label).
  - `void drawItemStack(Hud&, const ItemStack&, float x, float y, float size, float brightness = 1.0f)`
- `drawInventory`, `drawItemStack`, `drawHotbar` deleted from main.cpp; the
  frame loop's hotbar/inventory draw calls go through the module (the
  first-person held-item render pass is a separate 3D system and is not
  touched). main.cpp keeps input handling, hit-testing dispatch,
  click/quick-move logic, and builds the view structs.
- No GL beyond the `Hud` API; no reads of main.cpp globals; no new
  dependencies. New .cpp added to the same CMake target(s) as Hud.cpp.

**Tests that must pass:**
- Full existing suite (the module is GL-bound, so coverage is compile +
  behavior preservation; world_tests must stay green).

**Validation:** `cmake --build build -j && .\build\world_tests.exe` →
exit 0, `all tests passed`; `.\build\groundwork.exe --demo-inv --frames 240`
→ exit 0, screenshot renders with inventory visible.
**Out of scope:** the Minecraft visual style (Task 5), demo flags (Task 6),
moving hit-testing or click logic.

### Task 5: Minecraft panel visual style          [M3]

**Files:** modify `src/ui/InventoryUi.cpp` (and `.h` for shared helpers)

**Behavior:** (spec R3, on the Task 3 geometry)
- Style helpers exported for reuse: `drawPanel(Hud&, const Rect&)` (raised
  bevel: ~3 px near-white top+left, ~3 px dark bottom+right, 1–2 px outer
  dark border, fill ≈ 0.78 gray, opaque) and
  `drawBeveledSlot(Hud&, const Rect&)` (inset bevel: ~2 px dark 0.35
  top+left, ~2 px near-white bottom+right, 0.55 gray center), and
  `drawArrow(Hud&, const Rect&)` (shaft + stepped-rect head pointing right,
  medium-dark gray).
- Inventory/crafting/furnace screens render: fullscreen dim kept, main
  panel + recipe panel via drawPanel, every slot via drawBeveledSlot,
  player box as dark (≈0.05) inset rect on the inventory screen, arrow
  into the output slot, title text ("Inventory" / "Crafting" / "Furnace")
  top-left inside the panel in dark ≈0.25 gray.
- Furnace flame/progress indicators keep their data sources
  (`burnTicksRemaining`, `cookTicks`) restyled to sit in the new top
  section.
- Item stacks/counts/durability render via the relocated `drawItemStack`,
  unchanged logic. In-game hotbar keeps its current style (now 9 slots).

**Tests that must pass:**
- Full existing suite stays green (visuals are screenshot-verified).

**Validation:** `cmake --build build -j && .\build\world_tests.exe` →
exit 0, `all tests passed`; `.\build\groundwork.exe --demo-inv --frames 240`
→ screenshot converted to PNG and inspected: light-gray beveled panel,
player box, 2×2 grid + arrow + output, 3×9 grid, separated hotbar row.
**Out of scope:** crafting-table/furnace demo flags (Task 6), layout
changes (geometry is fixed by Task 3).

### Task 6: Demo flags, screenshots, docs          [M3]

**Files:** modify `src/app/main.cpp`, `ROADMAP.md`,
`docs/handoff/STATUS.md`

**Behavior:**
- **Save isolation (mandatory, all demo flags):** when any `--demo-*` flag
  is active, the run must not create or modify anything under the user's
  real `saves/` — concretely: the exit-path `savePlayer()` and
  `world.saveAllModified()` calls are skipped for demo runs (and any other
  demo-triggered persistence likewise). This covers the existing
  `--demo-inv`/`--demo-survival`/etc. flags too, which today persist their
  demo state into `saves/world1` on exit.
- `--demo-craft`: following the `--demo-inv` pattern — survival mode,
  stocked inventory, opens the crafting-table screen (3×3) at startup.
- `--demo-furnace`: places a furnace block at a known position near the
  player, creates its `FurnaceState` block entity with items in input and
  fuel (fuel consumed so `burnTicksRemaining > 0` and `cookTicks` advance —
  flame and progress indicators visibly render), then opens the furnace
  screen targeting that exact block position so `openFurnaceState()`
  resolves non-null.
- `ROADMAP.md` + `docs/handoff/STATUS.md` record: panel restyle, InventoryUi
  extraction, 9-column inventory, player save v4, demo-flag save isolation
  (spec R6).

**Tests that must pass:**
- Full suite green; no new headless tests required (flags are GL-path).

**Validation:** `.\build\groundwork.exe --demo-craft --frames 240` and
`.\build\groundwork.exe --demo-furnace --frames 240` → both exit 0, write
`screenshot.ppm`; PNGs inspected: 3×3 panel screen and furnace panel
screen in the new style. Save-isolation check: record `saves/world1` file
listing + last-write times before the demo runs and assert they are
unchanged after (no new files, no modified files).
**Out of scope:** new gameplay; recipe changes; pause-menu work.
