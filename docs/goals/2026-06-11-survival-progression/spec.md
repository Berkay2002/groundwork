# Batch I Survival Progression Spec

## 1. Approved Direction

Batch I turns survival mode into the default game loop. It follows Minecraft
Java Edition's early survival progression where that fits the current
Groundwork engine: gather logs by hand, craft planks/sticks/crafting table,
make tools, mine cobblestone, craft a furnace, use coal for torches and fuel,
smelt raw iron, craft iron tools, mine diamond ore, and craft diamond tools.

The batch must make long-term choices. Inventory cannot stay block-only.
Crafting and furnace behavior cannot be fake one-off UI code. Tools and mining
must use data-driven item/block definitions so later swords, hoes, armor, mobs,
containers, and data-file migration do not require rewriting the foundation.

## 2. Item Model

Add a separate append-only item identity model, independent from saved chunk
`Block` bytes.

Requirements:
- Item ids are saved as a wider item identity, not raw `Block` ids. `uint16_t`
  is acceptable.
- Blocks that can be held or placed have item definitions that point to their
  placeable `Block`.
- Non-block items include at least: Stick, Coal, Raw Iron, Iron Ingot, Diamond,
  and all wood/stone/iron/diamond pickaxes, axes, and shovels.
- Item definitions carry stable behavior metadata:
  - display name
  - stack max
  - optional placeable block
  - optional tool class
  - optional tool tier
  - max durability
  - mining speed
  - fuel burn ticks or fuel smelt count where applicable
  - icon id or equivalent procedural icon reference
- Tools are non-stackable. Normal material and block items stack to 64 unless
  explicitly defined otherwise.
- One `ItemStack` shape is used for all stacks and includes item id, count, and
  durability. Only durable items use durability now, but the save format writes
  it for every stack.
- Empty hand is modeled as the implicit mining tool with speed 1, no tool
  class, no durability, and no item id.

## 3. Block Model and Content

Existing block enum values stay append-only. Do not renumber saved values.

Add blocks:
- Cobblestone
- Planks
- Crafting Table
- Furnace
- Diamond Ore

Existing `Wood` remains the saved block id but is treated as Log in gameplay
and UI.

Block definitions distinguish:
- breakability
- hardness
- preferred/correct tool class
- minimum harvest tier for useful drops
- correct-harvest item drop and count
- wrong-harvest drop, usually empty
- sound material
- tile mapping
- solidity/collision/opacity/light behavior

Minecraft-like content rules:
- Stone terrain remains Stone. Harvesting Stone correctly drops Cobblestone.
- Cobblestone is placeable and used for stone tools and furnace recipes.
- Coal Ore drops Coal when harvested with a pickaxe.
- Iron Ore drops Raw Iron when harvested with a stone-tier or better pickaxe.
- Diamond Ore drops Diamond when harvested with an iron-tier or better pickaxe.
- Logs drop Log. Logs craft into Planks.
- Leaves still drop nothing.
- Torches drop Torch and are craftable from Coal + Stick.
- Bedrock remains unbreakable.
- Crafting Table is axe-preferred, hand-breakable, and drops itself.
- Furnace is pickaxe-preferred, requires a pickaxe for a useful Furnace drop,
  and drops its contents when broken.

Initial block tuning:

| Block | Hardness | Preferred tool | Min useful harvest tier | Correct drop | Wrong-tool drop |
|---|---:|---|---|---|---|
| Grass | 0.6 | Shovel | Hand | Dirt x1 | Dirt x1 |
| Dirt | 0.5 | Shovel | Hand | Dirt x1 | Dirt x1 |
| Sand | 0.5 | Shovel | Hand | Sand x1 | Sand x1 |
| Log (`Wood`) | 2.0 | Axe | Hand | Log x1 | Log x1 |
| Leaves | 0.2 | None | Hand | empty | empty |
| Torch | 0.0 | None | Hand | Torch x1 | Torch x1 |
| Planks | 2.0 | Axe | Hand | Planks x1 | Planks x1 |
| Crafting Table | 2.5 | Axe | Hand | Crafting Table x1 | Crafting Table x1 |
| Stone | 1.5 | Pickaxe | Wood | Cobblestone x1 | empty |
| Cobblestone | 2.0 | Pickaxe | Wood | Cobblestone x1 | empty |
| Coal Ore | 3.0 | Pickaxe | Wood | Coal x1 | empty |
| Iron Ore | 3.0 | Pickaxe | Stone | Raw Iron x1 | empty |
| Diamond Ore | 3.0 | Pickaxe | Iron | Diamond x1 | empty |
| Furnace | 3.5 | Pickaxe | Wood | Furnace x1 | empty |
| Bedrock | unbreakable | None | None | empty | empty |
| Water | not breakable | None | None | empty | empty |

## 4. Tool and Mining Rules

Use Minecraft-like unenchanted, on-land mining rules for the supported blocks,
without status effects or environment penalties.

Tool classes:
- Pickaxe
- Axe
- Shovel

Tool tiers:
- Hand
- Wood
- Stone
- Iron
- Diamond

Tier speed and durability:
- Hand: speed 1, infinite durability
- Wood: speed 2, durability 59
- Stone: speed 4, durability 131
- Iron: speed 6, durability 250
- Diamond: speed 8, durability 1561

Mining behavior:
- Breaking is timed at the fixed 20 TPS simulation cadence.
- Continuing to hold the break button on the same target advances break
  progress. Releasing, changing target, changing mode, opening menus, or moving
  out of reach cancels or resets progress.
- Creative mode keeps instant breaking and no drops.
- Survival uses hardness, preferred tool class, tool tier, and tick rounding.
- A correct tool speeds mining. A wrong tool may still break many blocks but
  drops no useful item when the block requires a tool/tier.
- A tool loses 1 durability when it successfully breaks a non-instant block,
  even if the wrong tool produced no useful drop. Empty hand loses nothing.
- A tool at zero durability breaks and disappears.
- Combat/tool attack behavior is out of scope, but durability use must be
  routed through a helper that can later support use reasons.

Exact break tick model:
- Blocks with `hardness == 0` break immediately and cost 0 durability.
- Unbreakable blocks never progress.
- `correctTool` is true when the selected tool's class matches the block's
  preferred tool. Blocks with preferred tool `None` use the hand-speed path.
- `canHarvestUsefulDrop` is true when the block's minimum harvest tier is Hand,
  or when `correctTool` is true and the selected tool tier is at least the
  block's minimum harvest tier.
- If `correctTool && canHarvestUsefulDrop`, required ticks are
  `ceil(hardness * 30 / toolSpeed)`.
- Else if `canHarvestUsefulDrop`, required ticks are `ceil(hardness * 30)`.
- Else required ticks are `ceil(hardness * 100)` and the wrong-tool drop is
  used.
- The selected tool's speed is the tier speed listed above; non-tools and empty
  hand use speed 1.
- Tick counts are integer simulation ticks at 20 TPS. Tests assert the integer
  tick count, not rounded seconds.

Reference examples from the model:
- Dirt by hand: `ceil(0.5 * 30) = 15` ticks.
- Dirt with diamond shovel: `ceil(0.5 * 30 / 8) = 2` ticks.
- Log by hand: `ceil(2.0 * 30) = 60` ticks.
- Log with wood axe: `ceil(2.0 * 30 / 2) = 30` ticks.
- Stone by hand: `ceil(1.5 * 100) = 150` ticks and empty drop.
- Stone with wood pickaxe: `ceil(1.5 * 30 / 2) = 23` ticks and Cobblestone.
- Cobblestone with stone pickaxe: `ceil(2.0 * 30 / 4) = 15` ticks.
- Iron Ore with wood pickaxe: `ceil(3.0 * 100) = 300` ticks and empty drop.
- Iron Ore with stone pickaxe: `ceil(3.0 * 30 / 4) = 23` ticks and Raw Iron.
- Diamond Ore with stone pickaxe: `ceil(3.0 * 100) = 300` ticks and empty drop.
- Diamond Ore with iron pickaxe: `ceil(3.0 * 30 / 6) = 15` ticks and Diamond.

User feedback:
- The targeted block shows procedural crack stages while breaking.
- A HUD progress indicator near the crosshair is allowed as fallback or
  supplemental feedback, but the crack overlay is required.
- Break and place sounds continue to use block sound material.

## 5. Crafting

Crafting is part of Batch I. Batch L can later deepen recipes, data files, and
recipe-book behavior.

Crafting surfaces:
- Player inventory has a 2x2 crafting grid and output slot.
- Crafting Table opens a 3x3 crafting grid and output slot.
- Crafting Table recipes require the table when they need 3x3 space.
- Crafting state does not need to persist when the UI closes; any items left in
  transient crafting grids must return to inventory or drop if inventory is
  full.

Required recipes:
- Log -> Planks
- Planks -> Sticks
- Planks -> Crafting Table
- Cobblestone -> Furnace
- Planks + Sticks -> wooden pickaxe, axe, shovel
- Cobblestone + Sticks -> stone pickaxe, axe, shovel
- Iron Ingots + Sticks -> iron pickaxe, axe, shovel
- Diamonds + Sticks -> diamond pickaxe, axe, shovel
- Coal + Stick -> Torches

Exact recipe table:

| Surface | Shape | Output |
|---|---|---|
| 2x2 or 3x3 | one Log anywhere, all other cells empty | Planks x4 |
| 2x2 or 3x3 | vertical pair of Planks | Sticks x4 |
| 2x2 or 3x3 | 2x2 square of Planks | Crafting Table x1 |
| 2x2 or 3x3 | Coal above Stick | Torch x4 |
| 3x3 only | Cobblestone in every slot except center | Furnace x1 |
| 3x3 only | Pickaxe: material across top row, Sticks in center and bottom-center | matching pickaxe x1, full durability |
| 3x3 only | Axe: material in top-left, top-center, middle-left, Sticks in middle-center and bottom-center | matching axe x1, full durability |
| 3x3 only | Shovel: material in top-center, Sticks in middle-center and bottom-center | matching shovel x1, full durability |

Tool material items:
- Wooden tools use Planks.
- Stone tools use Cobblestone.
- Iron tools use Iron Ingots.
- Diamond tools use Diamonds.

Recipe implementation:
- Hardcoded C++ tables are acceptable for Batch I.
- Tables must be data-shaped: item ids, grid size, shaped pattern, output item,
  and output count. They should be straightforward to move to data files later.
- Recipe matching and crafting consumption are pure logic and covered by
  headless tests.

## 6. Furnace and Block Entities

Furnace is a real placeable block with persistent state. This introduces the
smallest block-entity layer needed for furnace behavior, without building the
full Batch O/P container or block-state systems.

Requirements:
- Furnace state is keyed by world position.
- State includes input stack, fuel stack, output stack, burn/cook progress, and
  enough remaining burn time to survive save/load.
- Furnace state persists separately from raw chunk block bytes.
- State is removed when the furnace block is removed.
- Breaking a furnace drops the furnace item when harvested correctly and drops
  its input/fuel/output contents as item entities.
- Coal is a fuel. One coal provides 8 smelts worth of burn time.
- Furnace consumes fuel only when it has smeltable input and output space.
- Raw Iron smelts into Iron Ingot.
- Furnace logic is pure enough for headless tests. GL and HUD code only render
  and interact with the state.

Exact furnace timing and save behavior:
- Furnace ticking runs on the same 20 TPS simulation cadence as player/entity
  logic.
- Raw Iron -> Iron Ingot takes 200 ticks.
- Coal burn time is 1600 ticks, exactly 8 full Raw Iron smelts.
- Furnace state fields are input stack, fuel stack, output stack,
  `burnTicksRemaining`, and `cookTicksDone`.
- Cook progress advances only when input has a matching furnace recipe and
  output is empty or merge-compatible with space.
- If no burn time remains and smelting can proceed, one fuel item is consumed
  and `burnTicksRemaining` increases by that fuel's burn ticks.
- If `burnTicksRemaining > 0`, one burn tick is consumed on every furnace tick
  whether or not input/output currently allows cooking. Lit furnaces therefore
  waste fuel when output becomes blocked or input is removed.
- `cookTicksDone` advances only while smelting can proceed. It resets to 0 when
  input changes, output becomes blocked, or smelting cannot proceed.
- Furnace block-entity data saves to `saves/world1/block_entities.bin` using
  magic `MCBE`, version 1, and atomic writes through `SaveIO`.
- Each saved furnace entry stores int32 world x/y/z, the three item stacks, and
  int32 burn/cook tick counters. Bad magic/version or malformed entries are
  rejected safely, leaving no furnace states loaded.
- `World::saveAllModified()` and clean shutdown save block entities together
  with chunks/player. Loading a world loads block entities after construction
  and before gameplay ticks use them.

## 7. Inventory and UI

Survival is the default for fresh settings. Existing settings files remain
respected. Creative remains available through settings and a temporary runtime
mode toggle key.

Controls:
- Add rebindable `key_mode_toggle`, default `M`.
- `M` toggles survival/creative at runtime and saves settings.
- Creative keeps the current fixed infinite block palette/debug behavior.
- Survival uses item inventory, tools, crafting, furnace, timed mining, and
  finite placement.

Inventory behavior:
- Inventory stores `ItemStack`, not `Block` stacks.
- Hotbar slot positions remain 0..7. Existing selected slot is preserved
  during save migration if in range.
- Left-click picks up, places, swaps, or merges whole stacks.
- Right-click splits a stack when picking up.
- Right-click places one item when carrying a stack.
- Crafting output click consumes recipe inputs and produces output.
- Shift-click quick-move is required where the destination is unambiguous:
  between player inventory and an open furnace/table surface. If a case is
  ambiguous, it should be explicitly ignored rather than guessed.
- Drag-painting stacks is out of scope.
- Tools show durability bars on any item with max durability > 0. Counts are
  hidden for tools and shown for stackable items when useful.

UI surfaces:
- Inventory/crafting/furnace UI must be drawn through `Hud`/existing UI
  primitives, not ad-hoc GL.
- A simple read-only recipe reference panel is included, generated from the
  hardcoded recipe table. It does not need search, unlock tracking, or a full
  Minecraft recipe book.
- Right-clicking a Crafting Table opens 3x3 crafting UI.
- Right-clicking a Furnace opens furnace UI.
- Interacting with targeted Crafting Table/Furnace takes priority over placing
  against it unless the player is sneaking.

Exact click rules:
- Left-click a normal slot with empty cursor: pick up the whole stack.
- Left-click a normal slot with carried stack: place into empty slot, merge as
  much as possible into a compatible stack, or swap with an incompatible stack.
- Right-click a normal slot with empty cursor: take `ceil(count / 2)` from the
  slot, leaving `floor(count / 2)` behind.
- Right-click a normal slot with carried stack: place exactly one item into an
  empty compatible slot, or add one to a compatible non-full stack. It is a
  no-op on incompatible slots.
- A compatible stack has the same item id and the same durability value for
  durable items. Items with `stackMax == 1` do not merge.
- Crafting output left-click or right-click crafts one result if the cursor is
  empty or can merge the result. Shift-click crafting output crafts repeatedly
  until inputs, output capacity, or destination space runs out.
- Furnace output uses the same output-pickup rules. Shift-click furnace output
  moves to player inventory.
- Furnace fuel slot accepts fuel items only. Furnace input accepts smeltable
  inputs only for direct placement. Existing invalid input from a bad/migrated
  save is inert and can be removed.
- Shift-click from player inventory into a furnace tries fuel slot first for
  fuel, input slot first for smeltable input, and otherwise no-ops.

## 8. Entities and Drops

World item drops remain the primary acquisition path.

Requirements:
- Item entities carry `ItemStack`, not `Block + count`.
- Block item drops can keep rendering as small cubes.
- Non-block item drops render as procedural icon billboards.
- Item entities merge with nearby compatible stacks up to stack max.
- Tools do not stack or merge.
- Item pickup adds item stacks into inventory, preserving durability.
- Full inventory leaves the uncollected remainder in the world.
- Entity persistence remains out of scope for Batch I, as Batch J owns durable
  item entities.

Exact merge rules:
- Merge runs inside `Entities::tick` after physics/pickup/despawn processing
  and before rebuilding chunk buckets.
- Only entities whose chunk area is ready participate. Frozen entities in
  unloaded chunks neither move nor merge.
- Merge radius is 0.75 blocks between entity body positions.
- Earlier entities in `items_` absorb later compatible entities until the
  earlier stack reaches its item max. Any remainder stays in the later entity.
- Compatibility means same item id, item stack max greater than 1, and matching
  durability for durable items. Tools have stack max 1 and never merge.

## 9. Procedural Visuals

Keep the no-asset-files property.

Requirements:
- New block textures are procedural in the existing texture-generation style.
- Non-block item icons are procedural or embedded in code like existing assets.
- Tool icons use material/tier tinting or equivalent procedural art, not files.
- Crack overlay stages are generated procedurally and rendered as a separate
  targeted-face overlay. They must not be baked into chunk meshes.
- Visual changes get screenshot verification. Golden reference updates are
  allowed only after intentional visual inspection.

## 10. Terrain Generation

Add Diamond Ore to deterministic terrain generation.

Requirements:
- Diamond Ore generation is a pure function of world coordinates and seed.
- It must preserve chunk-order independence.
- It should be rare and deep, below iron's general band.
- Coal and iron generation remain mostly unchanged unless tests or gameplay
  show they are obviously off.
- Tests cover diamond generation constraints and block registry consistency.

## 11. Save Migration

Player save format must migrate old inventory data.

Requirements:
- v2 block-stack player saves migrate to the new item-stack format.
- Slot positions are preserved.
- Air stays empty.
- Existing block stacks become corresponding block item stacks.
- Existing Stone stacks become Cobblestone item stacks.
- Existing Coal Ore and Iron Ore stacks remain their block-item equivalents if
  present; they do not convert into Coal/Raw Iron.
- Old saves get no free tools.
- New stacks write durability fields. Existing migrated non-tool stacks have no
  durability.
- Tests cover migration and roundtrip.

World/chunk saves:
- Chunk block ids remain append-only and versioned as before.
- If adding blocks requires a chunk version bump, document why. Prefer avoiding
  layout changes that force old chunks to regenerate.
- Furnace block-entity saves must reject bad/old headers safely.

## 12. Verification

Required logic tests:
- item registry consistency, stack max, block placement mapping, fuel values,
  durable item metadata
- v2 player inventory migration to item stacks and new save roundtrip
- 2x2 and 3x3 recipe matching, crafting consumption, and output behavior
- inventory left/right-click split/place/merge and relevant shift-click moves
- harvest rules for soft blocks, stone/cobblestone, coal, iron, diamond,
  crafting table, furnace, bedrock
- break-time/tick progression and target reset behavior where practical
- durability decrement and tool breakage
- furnace smelting, fuel consumption, output blocking, and persistence
- furnace breaking drops contents
- item entity stack merging and non-stackable tools
- diamond ore generation constraints

Required commands:
- warning-free build
- `world_tests` passing
- survival/demo screenshot rendered and visually inspected

Documentation:
- `ROADMAP.md` marks Batch I done and records what actually landed.
- `README.md` documents survival default, mode toggle, crafting/furnace
  controls, inventory clicks, tool tiers, and progression.
- `docs/handoff/STATUS.md` records implementation notes and verification
  evidence.

## 13. Explicit Non-Goals

- Mobs, health, hunger, death, respawn, armor, swords, combat, and damage.
- Chests, general containers, and broad block-state framework beyond furnace
  block entities.
- Gold/netherite, enchantments, Haste, Mining Fatigue, underwater/airborne
  mining penalties, silk touch, charcoal, repair/anvil.
- Full creative inventory.
- Full recipe book search, unlocks, drag-painting stacks, and data-file
  recipes.
- Entity persistence and item cleanup beyond merging, because Batch J owns
  durable item entities.

## 14. M5 Addendum: Visual Polish and Held Item (2026-06-12)

Reopened per user request, same goal/batch. Replaces the placeholder-quality
procedural art from Task 10 and adds first-person held-item rendering.

### 14.1 Item and tool icon art

- Replace the geometric icon functions in `src/render/Texture.cpp` with
  explicit 16x16 ASCII sprite maps (one string row per pixel row, palette
  char -> RGB) for: stick, coal, raw iron, iron ingot, diamond, and one
  sprite per tool class (pickaxe/axe/shovel) with a per-tier material
  palette (wood/stone/iron/diamond). Minecraft-like reading: diagonal
  handle to bottom-left, head at top-right, 1px dark outline, subtle
  per-pixel noise shading.
- Sprites are authored top-down (row 0 = visual top) and flipped at lookup:
  tile-space y=0 is the visual bottom (HUD flips v; mesher maps v=1 to the
  face top). Document this convention once in Texture.cpp.
- Item icons get transparent backgrounds. The HUD strip atlas and the block
  texture array become RGBA8 (blocks fully opaque); the HUD textured mode
  multiplies by texture alpha; the item-entity shader discards alpha < 0.5
  so dropped tools render as cut-out sprites instead of dark squares.
- The torch tile background becomes transparent in the same way (the 3D
  torch post samples only the central strip; the hotbar/dropped icon wins).
- Fix the dropped-item V orientation: `buildCube`/`buildBillboard` in
  ItemRenderer currently sample v=1 (visual top row) at the bottom vertices,
  rendering art upside down. Top vertices must sample v=1.

### 14.2 Workstation and block faces

- Crafting table top: lighter worktop planks, dark border frame, dark 3x3
  grid lines. Side: planks with a dark top band and simple dark tool
  silhouettes (saw + hammer reading). Bottom stays Planks.
- Furnace: side/top become smooth gray stone slabs visually distinct from
  cobblestone; front = side art with a dark recessed mouth near the bottom.
  `BLOCK_DEFS` shows `FurnaceFront` on all four side faces (top/bottom use
  the side tile). No facing metadata — accepted simplification, documented
  in code. Block save bytes unchanged (TileId mapping is renderer-only).
- Cobblestone becomes irregular staggered stones (per-stone brightness,
  darker mortar) instead of a flat aligned grid.
- Ore tiles keep their mineral colors but gain subtle blob edge shading.

### 14.3 Break-crack overlay redesign

- Crack stages become pixelated Minecraft-style damage: hard-edged ~1px
  cracks that lengthen/branch per stage plus crumble speckles whose density
  grows with stage, clustered near the cracks. No soft anti-aliased halo;
  alpha quantized to a small set of levels.
- `BREAK_CRACK_STAGES`, `breakStageForProgress`, and the BreakOverlay API
  are unchanged; only `crackAlphaPixel` art changes.

### 14.4 First-person held item

- New render pass after the water/crack passes and before the HUD: the
  selected stack renders bottom-right as a viewmodel. Survival shows the
  selected inventory stack; creative shows the held palette block; empty
  hand renders the arm cuboid.
- Block items render as a mini cube with per-face tiles; non-block items
  render as the flat icon sprite (alpha cut-out, two-sided). An empty hand
  renders a simple first-person arm cuboid with a procedural skin tile
  (user reference image, 2026-06-12).
- Own projection (fixed FOV, screen aspect); depth cleared or disabled for
  the pass so the item never clips into world geometry.
- Lit like item entities: world sun/block light at the player eye cell,
  sun channel scaled by the day/night level.
- Simple swing animation while mining (survival, break held) and a brief
  swing pulse on place/click; implemented as a pure phase->transform curve
  helper testable headless where practical.

### 14.5 Verification

- Warning-free build; `world_tests` all pass.
- Inspected screenshots: `--demo-survival` (new hotbar icons, held tool,
  staged crafting table/furnace, cracks), `--demo-inv` (icons in the grid),
  `--demo-items` (dropped sprite cut-outs, right side up), and a default
  creative run (held block, golden-view impact).
- Golden reference regenerated only after visual inspection (hotbar torch
  and water icons change it by construction).
- `ROADMAP.md`/`README.md` notes where relevant and
  `docs/handoff/STATUS.md` updated.

### 14.6 Out of scope (M5)

- Furnace facing metadata or block state.
- Walk-bob synchronization and MC-style sprite extrusion (3D thickness)
  for held/dropped items. (The arm cuboid for an empty hand moved INTO
  scope on 2026-06-12 user reference images; spec/plan review skipped at
  the user's explicit instruction.)
- Any asset files.
