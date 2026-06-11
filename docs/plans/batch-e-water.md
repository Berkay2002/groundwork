# Batch E water plan

Water is the structurally hard part of Batch E because it is the first block
that is neither air nor a normal cube: it needs its own row in the predicate
matrix, a translucent second mesh pass, and a decision about sunlight.

## Block + predicate matrix

`Block::Water = 11` (appended after CoalOre=9 / IronOre=10 — save stays v1).

| predicate | value | why |
|---|---|---|
| `isSolid` | **false** | raycast passes through water; placing a block into a water cell replaces the water (only way to remove static water) |
| `isCollidable` | **false** | swim-through |
| `isOpaque` | **false** | light passes, mesher draws lakebed faces against water |
| `isBreakable` | n/a | never targetable |
| `lightEmission` | 0 | |
| new: `dimsSunlight` | **true** (water only) | see lighting |

## Lighting: water attenuates sunlight

Decision: like Minecraft, water breaks the "level-15 sunlight propagates
downward losslessly" rule, so depth darkens.

- `Chunk::computeInitialLight` column fill stops at water as well as opaque
  (water never gets a free 15); the BFS then spreads into water from the lit
  air above with normal −1 attenuation → surface 14, then 13, 12… per block
  of depth.
- Both lossless-down special cases (`d.y == -1 && level == 15`) in the chunk
  BFS and `World::addLight` get `&& !dimsSunlight(destination)`. The unlight
  BFS needs no change: water under a 15 column is always < 15, so the
  `nl < l` branch already strips it.
- Block light (torches) treats water like any transparent cell.

## Meshing: second translucent pass

- `MeshData` gains `waterVerts`/`waterInds`; `buildMeshData` routes water
  cells there. Face rule: emit a water face when the neighbor is **not water
  and not opaque** (air, torch); cull water-water and water-stone. Opaque
  blocks keep their existing rule, so lakebeds render under water.
- Water cells are full cubes (no surface lowering — keeps side faces gapless).
- `Chunk` gets a second VAO/VBO/EBO + `drawWater()`; `uploadMesh` uploads
  both. `World::drawWater(frustum)` is a second loop; main.cpp draws all
  opaque chunks first, then enables `GL_BLEND`, disables face culling (so the
  surface is visible from underwater), and draws the water pass with a new
  `uAlpha` uniform (~0.65) in the existing chunk shader (1.0 for opaque pass).
  Depth writes stay on: water-water faces are culled so a lake is one layer,
  and unsorted overlap artifacts are acceptable/MC-like.

## Generation: lake basins

Terrain currently bottoms out at height 19 for seed 1337, so a sea level of
20 alone would give puddles. `heightAt` gets a low-frequency basin mask
(smoothstepped fBm, ~1/192 freq) that depresses terrain by up to ~14 blocks
where strong → real lakes a few chunks wide, up to ~10 deep. `SEA_LEVEL = 20`
(just under `SAND_LEVEL = 21`, so shores are sand). `generateChunk` fills
air from `height+1` up to `SEA_LEVEL` with water.

Consequences accepted: this changes `heightAt` globally, so unmodified
terrain regenerates differently (the user's 3 modified chunks are preserved
and may sit as old-terrain islands — flagged in the summary). Caves must not
drain lakes: columns with `height < SEA_LEVEL + 2` are not carved above
`height − 4`.

## Player

Minimal swim so deep lakes aren't a trap (jump impulse alone can't escape
>1.5 blocks of water): when the player's body is in water — reduced gravity,
slow terminal velocity (~−4), and holding jump swims upward. No other
physics changes.

## UI

Water gets atlas tile 12 (blue + noise waves), a hotbar slot (8 slots total),
and a `blockName` entry. The debug overlay never shows water as target
(not raycastable) — fine.

## Tests

- Predicate matrix row; water bytes round-trip through save/load.
- A generated lake column: water from height+1 to SEA_LEVEL, sand bed.
- Sunlight: 15 above surface, 14 in the top water cell, decreasing with depth;
  restoring after placing/removing a block over water.
- Mesh: water faces land in `waterInds`, water-water and water-stone culled,
  lakebed stone face still in the opaque mesh.
