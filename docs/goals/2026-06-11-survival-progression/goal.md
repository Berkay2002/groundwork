# Goal: Batch I Survival Progression

**Created:** 2026-06-11
**Design approved by user:** yes (2026-06-11)

## Objective

Build Batch I so survival mode has a real Minecraft-like resource loop instead
of creative placement with stack counts. The player starts with an empty
inventory, gathers logs by hand, crafts planks, sticks, a crafting table,
tools, a furnace, torches, iron tools, and finally diamond tools. The work must
use long-term foundations: a separate item registry, durable item stacks,
tool classes and tiers, block harvest rules, timed breaking, crafting,
furnace state, Minecraft-style basic inventory interactions including
right-click stack splitting and shift-click quick-move, and procedural
icons/art without adding asset files.

## Stopping Condition

Batch I is complete when survival is the default for fresh settings and a new
player can complete this Minecraft-like progression loop from an empty
inventory: log -> planks/sticks -> crafting table -> wooden tools ->
cobblestone -> stone tools/furnace -> coal/torches -> raw iron -> iron ingot
-> iron tools -> diamond ore -> diamond tools. The implementation is committed
and `cmake --build build -j` exits 0 without warnings, `./build/world_tests`
prints `all tests passed`, a survival/demo `--frames` screenshot has been
rendered and inspected showing the survival UI and block breaking feedback, and
`ROADMAP.md`, `README.md`, and `docs/handoff/STATUS.md` document what landed.

## Validation Commands

- `cmake --build build -j` -> expected: exit 0, warning-free build.
- `./build/world_tests` or `.\build\world_tests.exe` -> expected: exit 0 and
  `all tests passed`.
- A survival/demo screenshot run, for example `./build/groundwork
  --demo-survival --frames 300` or the Windows equivalent -> expected: exit 0,
  writes `screenshot.ppm`, and visual inspection confirms survival UI plus
  block breaking feedback.

## Constraints

- Keep chunk save block ids append-only; never renumber existing `Block`
  values.
- Keep the no-asset-files property. Any new block textures, item icons, and
  crack overlays must be procedural or embedded like the current assets.
- Preserve the main-thread ownership rule: the main thread owns the chunk map,
  entities, block entities, and all GL; workers never touch them.
- Keep headless logic testable. Item, recipe, furnace, harvest, durability,
  save migration, and generation rules must not require a GL context.
- Do not delete or rewrite the user's real `saves/` directory. Temporary test
  saves created by this work must be cleaned up.
- Creative mode remains available as a fixed infinite block palette/debug mode.

## Out of Scope

- Mobs, health, hunger, death, armor, swords, and combat behavior.
- Chests and general container gameplay beyond the furnace state required by
  this batch.
- Full recipe book search/unlock, drag-painting inventory stacks, anvil/repair,
  enchantments, Haste/Mining Fatigue, underwater/airborne mining penalties,
  gold/netherite tiers, silk touch, charcoal, and data-file recipe loading.
- Full creative inventory and world selection/home-screen UX.

## M5 Addendum: Visual Polish and Held Item (2026-06-12)

User-requested follow-up in the same goal/batch: the Batch I procedural art
(tool/item icons, crafting table, furnace) reads as crude geometric
placeholders, the break-crack overlay lacks the Minecraft feel, and tools
never render in the player's hand (no first-person held-item pass exists).

**Objective:** Redesign the Batch I procedural art to read as Minecraft-like
pixel art (sprite-based item/tool icons with transparent backgrounds,
crafting-table and furnace faces, furnace front actually shown on the block,
crack stages), and add a first-person held-item render pass with a simple
swing so the selected tool/block is visible in hand.

**Stopping condition (addendum):** Committed implementation where
`cmake --build build -j` exits 0 warning-free, `world_tests` prints
`all tests passed`, and inspected `--frames` screenshots show: (a) the new
item/tool icons in the hotbar/inventory UI, (b) the crafting table and
furnace (with visible front) in the world, (c) the redesigned crack overlay
on a mined block, and (d) the selected tool rendered in the player's hand.
Golden reference regenerated only after visual inspection. Docs updated.

Same validation commands and constraints as above (no asset files, ids
append-only, main-thread GL ownership).

## Checkpoints

Progress is logged to `progress.md` after every task and milestone.
