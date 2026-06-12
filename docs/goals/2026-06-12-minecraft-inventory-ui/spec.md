# Spec: Minecraft-Style Inventory UI Panel

Goal contract: `goal.md` in this directory. Design approved by user 2026-06-12
(Approach B: dedicated InventoryUi module + 9-column migration).

## 1. Requirements

### R1 — 9-column inventory

- `Inventory::COLS` changes 8 → 9 (`SLOTS` = 36). Row 0 stays the hotbar.
- `PLAYER_VERSION` bumps 3 → 4. v4 saves 36 slots. Loading:
  - v4: read 36 slots directly.
  - v3: read 32 slots; place old slot `r*8+c` at new slot `r*9+c`
    (column 8 of each new row left empty). Contents and positions preserved.
  - v2: existing block-item migration, mapped through the same `r*8+c → r*9+c`
    placement.
  - v1: empty inventory (unchanged behavior).
  - Bad magic / unknown version: reject (unchanged behavior).
- Hotbar selection: keys 1–9 select slots 0–8 (the existing
  `GLFW_KEY_1 + HOTBAR_SLOTS` range check generalizes automatically once
  `HOTBAR_SLOTS` is 9). Scroll-wheel cycling wraps over 9 slots.
- Stale comments updated alongside: `Inventory.h` header comment
  ("4 rows x 8 columns… slot 0..7 = hotbar") and `PlayerSave.h` version
  comment ("Player persistence, v3").
- Creative palette `HOTBAR[]` gains a 9th block: `Block::Planks`
  (exists in `src/world/Block.h`, value 13).
- `hotbarSlot` loaded from any save version is clamped to valid range
  (existing behavior, now 0–8).

### R2 — Panel layout (`ui::` headers, pure logic)

New layout geometry in `src/ui/MenuUi.h` (header-only, pure, GL-free, as
today). The layout describes one centered panel per screen:

- **Inventory screen** (2×2 crafting):
  - Panel interior, top section (left→right): player-preview box (dark inset
    rect, ~2 slots wide × ~3.5 slots tall), 2×2 crafting grid, arrow,
    output slot.
  - Below: 3×9 main grid = inventory rows 1–3.
  - Gap of ~0.45 slot height, then the 9-slot hotbar row = inventory row 0.
- **Crafting-table screen** (3×3): same panel; the top section is the 3×3
  grid + arrow + output, horizontally centered; no player box.
- **Furnace screen**: same panel; top section is input slot above fuel slot,
  flame indicator between them, arrow to the output slot on the right.
  Burn/cook progress indicators keep their current data sources
  (`burnTicksRemaining`, `cookTicks`).
- **Recipe-reference strip**: a separate smaller panel attached to the right
  edge of the main panel, same style, holding the existing clickable recipe
  grid (3 per row, current behavior). It appears only on the crafting
  surfaces (inventory and crafting-table screens), as today; the furnace
  screen has no recipe panel.
- Panel size derives from slot size + paddings; the whole panel is centered
  on screen. Slot size stays 56 px with 4 px gaps (current values); at the
  default 1280×720 window the panel including the recipe side panel must fit
  on screen.
- All slot rects across a screen must be non-overlapping. Per surface, every
  slot kind *present on that surface* must round-trip through `uiSlotAt`
  (point inside rect → that slot): Inventory/Craft/CraftOutput/
  RecipeReference on the crafting surfaces; Inventory/Furnace on the furnace
  surface (which keeps returning `none()` for recipe slots, as today).
- Contractual signatures — unchanged because main.cpp dispatch and existing
  tests call them: `inventoryLayout`, `inventorySlotRect`, `inventorySlotAt`,
  `craftSlotRect`, `craftOutputRect`, `furnaceSlotRect`,
  `recipeReferenceSlotRect`, `recipeReferenceSlotAt`, `uiSlotAt`. Internals
  and produced geometry change freely; new helpers (e.g. `panelRect`,
  `playerBoxRect`, `arrowRect`) are added as needed by the renderer.

### R3 — Visual style (Minecraft look)

- Backdrop: keep the fullscreen dim (`0,0,0,0.55`).
- Panel: opaque light gray ≈ `(0.78, 0.78, 0.78, 1.0)` with a raised bevel:
  ~3 px near-white top+left edges, ~3 px dark-gray bottom+right edges, plus a
  1–2 px outer dark border so it reads against bright skies.
- Slots: inset bevel — ~2 px dark `(0.35)` top+left, ~2 px near-white
  bottom+right, medium-gray `(0.55)` center.
- Player box: plain dark inset rect ≈ `(0.05, 0.05, 0.05, 1.0)` with the
  slot-style bevel.
- Arrow: drawn from solid rects (shaft + triangular head approximated by
  2–3 stacked rects), medium-dark gray, pointing at the output slot.
- Title text ("Inventory" / "Crafting" / "Furnace") drawn inside the panel,
  top-left, dark gray ≈ `(0.25)`.
- Item stacks, counts, durability bars: rendered exactly as today
  (`drawItemStack` logic unchanged, just relocated).
- In-game hotbar (HUD while playing) keeps its current style, now 9 slots.

### R4 — InventoryUi module (Approach B)

New `src/ui/InventoryUi.h` + `src/ui/InventoryUi.cpp`, added to CMake:

- Owns screen composition: `drawInventoryScreen(...)`, plus the shared style
  helpers `drawPanel`, `drawBeveledSlot`, `drawArrow`, and the relocated
  `drawItemStack` and `drawHotbar`.
- Takes a `Hud&` plus a small read-only view struct (inventory, cursor stack,
  crafting state, screen kind, furnace state pointer, hotbar slot, survival
  flag, mouse position) — it must not reach into `main.cpp` globals.
- `main.cpp` keeps: input handling, hit-testing dispatch, click/quick-move
  logic, and calls the module from the frame loop. `drawInventory`,
  `drawItemStack`, `drawHotbar` are deleted from `main.cpp`.
- No GL calls in the module beyond the `Hud` API. No new dependencies.

### R5 — Tests (headless, in `tests/test_world.cpp`)

- Layout: for each surface (inventory 2×2, crafting 3×3, furnace) at
  1280×720, assert all slot rects are pairwise non-overlapping and that the
  center of every slot rect maps back to that slot via `uiSlotAt`
  (and `recipeReferenceSlotAt` where applicable). Assert the panel and the
  recipe panel fit on screen.
- Inventory: existing tests updated for 36 slots.
- Save migration: write a v3 file (32 slots, distinctive contents incl. a
  durable item), load it, assert each old slot `r*8+c` landed at `r*9+c`
  with identical item/count/durability; assert column 8 empty. Assert v4
  round-trip (save → load → identical). Use a temp path and delete it.
- Existing v1/v2 load tests: v1 unchanged; the v2 migration test is updated
  to assert the migrated contents land at the 9-column positions
  (`r*9+c`).
- **Visual verification mechanism**: two new demo flags in `main.cpp`,
  following the existing `--demo-inv` pattern: `--demo-craft` opens the
  crafting-table screen (3×3) with a stocked inventory, and
  `--demo-furnace` places a furnace block entity and opens the furnace
  screen (with items in input/fuel so the indicators render). Combined with
  `--frames N` these produce the three screenshots the stopping condition
  requires.

### R6 — Docs

`ROADMAP.md` and `docs/handoff/STATUS.md` updated to record the restyle, the
module extraction, and the v4 save bump.

## 2. Architecture

```
src/ui/MenuUi.h          pure layout + hit-testing + click/quick-move logic (unchanged role)
src/ui/InventoryUi.{h,cpp}  NEW: screen composition/drawing on top of Hud
src/ui/Hud.{h,cpp}       unchanged (low-level batcher)
src/app/main.cpp         input, click dispatch, frame loop; calls InventoryUi
src/sim/Inventory.h      COLS 8→9
src/sim/PlayerSave.h     PLAYER_VERSION 4 + v3→v4 migration
tests/test_world.cpp     layout + migration tests
```

Data flow per frame (inventory open): main.cpp builds an
`ui::InventoryView{...}` from app state → `ui::drawInventoryScreen(hud, view)`
→ Hud batches → `hud.end()`. Clicks: GLFW callback → `uiSlotAt` (MenuUi.h) →
existing click handlers in main.cpp. The module never mutates game state.

## 3. Error handling

- Save loading already fail-closed (reject on short read / bad header);
  migration follows the same pattern — any read failure returns false and the
  player regenerates at defaults.
- Layout has no failure modes; geometry is total. Window sizes smaller than
  the panel are out of scope (same as current behavior).

## 4. Out of scope

Player model rendering, armor/off-hand slots, chests, recipe search, pause
menu restyle, new inventory interactions, chunk format changes.

## 5. Milestones

- **M1 — 9 columns + save v4**: R1 + migration tests (R5 part).
- **M2 — Layout geometry**: R2 + layout tests (R5 part).
- **M3 — InventoryUi module + visuals**: R3 + R4 + the `--demo-craft` /
  `--demo-furnace` flags; all three screenshots inspected; R6.
