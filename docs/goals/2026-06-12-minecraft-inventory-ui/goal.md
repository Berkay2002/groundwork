# Goal: Minecraft-Style Inventory UI Panel

**Created:** 2026-06-12
**Design approved by user:** yes (2026-06-12)

## Objective

Restyle the inventory, crafting-table, and furnace screens to visually match
the classic Minecraft inventory panel: one opaque light-gray panel with a
raised bevel border, inset beveled slots, a dark player-preview placeholder
box, a 2x2 (or 3x3) crafting grid with a drawn arrow into the output slot, a
3x9 main grid, and a visually separated 9-slot hotbar row. Approach B was
approved: extract all inventory-screen drawing out of `main.cpp` into a new
`src/ui/InventoryUi.{h,cpp}` module (Hud stays the low-level batcher, layout
math stays pure in `MenuUi.h`). The inventory widens from 8 to 9 columns
(36 slots), with a player-save v4 migration that preserves slot positions, a
9th hotbar key, and a 9th creative-palette block.

## Stopping Condition

The goal is complete when the implementation in
`docs/goals/2026-06-12-minecraft-inventory-ui/spec.md` is committed and:
`cmake --build build -j` exits 0 warning-free; `world_tests` prints
`all tests passed` including new tests for 9-column layout hit-testing
(no overlapping slot rects, round-trip slot lookup on all three surfaces)
and the v3->v4 player-save migration (old slot row*8+col loads into new slot
row*9+col); inventory-screen drawing lives in `src/ui/InventoryUi.{h,cpp}`
and is no longer in `main.cpp`; `--frames` screenshots have been rendered,
converted, and visually inspected showing (a) the light-gray beveled panel
inventory screen with player box, 2x2 crafting grid, arrow, output slot,
3x9 main grid, and separated 9-slot hotbar row matching the reference look,
(b) the crafting-table screen with a 3x3 grid in the same panel style, and
(c) the furnace screen in the same panel style; key 9 selects the 9th hotbar
slot; the final verification report with fresh command output is written to
`docs/goals/2026-06-12-minecraft-inventory-ui/progress.md`; and docs
(`ROADMAP.md`, `docs/handoff/STATUS.md`) record what landed.

## Validation Commands

- `cmake --build build -j` -> expected: exit 0, warning-free.
- `.\build\world_tests.exe` (or `./build/world_tests`) -> expected: exit 0,
  prints `all tests passed`.
- `.\build\groundwork.exe --demo-survival --frames 300` (or equivalent demo
  flag that opens the inventory) -> expected: exit 0, writes
  `screenshot.ppm`; converted PNG visually inspected against the Minecraft
  reference layout.

## Constraints

- Preserve the pure-layout / GL-draw split: layout + hit-test math stays in
  `ui::` headers, testable headless without a GL context.
- `Hud` remains the low-level batcher (rects, tiles, text); no ad-hoc GL in
  the new module.
- No asset files: the panel look is built from `drawRect` bevels, not
  textures.
- Player save: bump `PLAYER_VERSION` to 4 with a v3->v4 migration that
  preserves inventory contents and positions; v1-v3 files must still load.
  Never corrupt or delete the user's real `saves/` directory; clean up any
  test saves.
- Chunk format (`CHUNK_VERSION`) and block ids untouched.
- Hit-testing and click/quick-move *behavior* unchanged apart from the new
  geometry (this is a rendering + layout change, not an interaction change).
- Warning-free build (`-Wall` / MSVC equivalent).

## Out of Scope

- Rendering a player model in the preview box (dark placeholder only).
- Armor slots, off-hand slot, chests, or any new container types.
- Recipe-book search/unlock; the existing recipe-reference strip is only
  restyled into a matching side panel.
- Pause/settings menu restyle (the bevel helpers may enable it later, but it
  is not part of this goal).
- Drag-painting stacks or other new inventory interactions.

## Checkpoints

Progress is logged to `progress.md` after every task and milestone.
