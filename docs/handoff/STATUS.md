# Status

Last updated: 2026-06-12, after authored-asset and living-entity foundation.

Groundwork is past MVP. The completed history is in `docs/goals/`; future work
is organized by roadmap area in `ROADMAP.md`. Do not start roadmap work without
user approval; follow `docs/handoff/WORKFLOW.md`.

## Current Contract

- C++17/OpenGL 3.3 voxel game with streaming chunks, background generation and
  meshing, saves, lighting, water, caves, ores, survival progression, audio,
  menus, day/night, runtime character assets, a first living entity, Windows
  support, and demo flags.
- Main thread owns the chunk map and all GL. Workers only build fresh chunks or
  CPU mesh data from immutable snapshots.
- `World` remains the chunk/streaming/save/fluid/block-entity hub. `Entities`
  owns dropped item storage, living entity storage, and dropped-item disk I/O.
  Simulation stores model ids only; asset loading and GL upload stay in
  `AssetManager`/render/app code. UI goes through `Hud` and `InventoryUi`.
- Saved ids are append-only. Never renumber blocks, items, or save bytes.

## Latest Decisions To Preserve

- Dropped items persist in `saves/world1/entities/e_<cx>_<cz>.bin` (`MCEN` v1).
  Bad entity files discard only that chunk's entities and warn.
- Unloaded item chunks do not tick, age, move, merge, despawn, or stay resident.
  Empty entity chunks delete their file on save.
- Demo runs may read saves but must not create, update, or delete them.
- Runtime asset files are allowed for authored content. Keep voxel block
  textures/HUD font procedural, but use `assets/manifest.json` plus
  source/license notes for characters and future authored assets. Current first
  model id: `creature.kenney_wanderer`.
- Living entities are not persisted yet. Dropped-item entity save files remain
  item-only.
- Water flow uses appended ids 24-31. Chunk integration seeds only flowing
  cells, not generated source water, so pristine lake chunks stay byte-stable.
- Shore exit is sustained `SHORE_HOP = 5.5` while jump + wall + water applies.
  Do not restore the earlier launchy impulse.
- Inventory is 4x9, player save is `MCPL` v4, and furnace facing/lit variants
  are appended block ids 17-23.

## Verification

Recent Windows/MSVC work built warning-free and `world_tests` passed. Added
coverage includes asset manifest/model loading, external PNG texture loading,
asset-manager caching, living spawn/tick/freeze/collision/damage/drop/query
behavior, MCEN persistence/corruption/isolation, entity unload/load and
autosave, demo save isolation, water spread/drain/infinite/drop-seek/save/shore
hop, and inventory/progression/furnace/UI migrations. Rendering changes were
visually inspected with demo screenshots. Latest TSAN was not run.

## Pointers

Verification recipes: `docs/handoff/VERIFICATION.md`. Roadmap: `ROADMAP.md`.
Completed-goal history: `docs/goals/`. Transient ignored state: `build/`,
`saves/`, `screenshot.*`, `settings.cfg`. World seed: 1337.
