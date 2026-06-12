# Status

Last updated: 2026-06-13, after creature persistence, player health, and
two-way melee combat.

Groundwork is past MVP. The completed history is in `docs/goals/`; future work
is organized by roadmap area in `ROADMAP.md`. Do not start roadmap work without
user approval; follow `docs/handoff/WORKFLOW.md`.

## Current Contract

- C++17/OpenGL 3.3 voxel game with streaming chunks, background generation and
  meshing, saves, lighting, water, caves, ores, survival progression, audio,
  menus, day/night, runtime character assets, deterministic ambient creature
  spawning, persistent hostile creatures, player health with two-way melee
  combat, Windows support, and demo flags.
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
  model ids: `creature.kenney_zombie_a` and `creature.kenney_zombie_b`.
- Normal worlds spawn the first creature through deterministic ambient
  seed-and-chunk rules. Repeated load events for an active chunk do not duplicate
  ambient creatures.
- Living creatures persist as MCEN record type 2; record type 3 is a per-chunk
  "ambient spawn consumed" marker, so each chunk ambient-spawns exactly once
  per world, ever — killed zombies stay dead. The MCEN version stays 1: the v1
  loader reads-and-skips unknown record types, so old item-only files remain
  loadable and new types stay append-only. A marker-only file is NOT deleted by
  the empty-file cleanup. Creatures unload/save with the chunk they stand in
  (the old home-chunk removal rule is gone; the marker prevents duplicates).
  Combat state (attack cooldown, hurt/knockback ticks) is runtime-only.
- Zombies are hostile: wander → chase (12-block radius + voxel line of sight)
  → melee (2 HP, 1 s cooldown, knockback) against a live survival player.
  Creative players, dead players, and demo runs are ignored (main passes a
  null target). Knockback on the player goes through a decaying accumulator
  (Player::applyKnockback) because walking overwrites horizontal velocity
  every tick; knocked-back creatures skip AI steering during hurtTicks for the
  same reason.
- Player health: 20 HP, MCPL v5 (appended i32 health; v1-v4 load at full, out
  of range clamps). Passive regen 1 HP/3 s after 5 s undamaged, 0.5 s
  i-frames, hearts HUD above the hotbar (survival only). Death respawns at
  world spawn; `keep_inventory` (settings.cfg + pause menu, default on) keeps
  items — off spills them as dropped items at the death point. Falling out of
  the world is death in survival (Player sets `outOfWorld`; the app decides).
- Player melee: left click attacks the nearest living entity within 3.5 blocks
  (ray-vs-AABB; the entity must be nearer than the targeted block), 3 HP +
  knockback. A creature in front of the crosshair blocks mining progress.
  Zombies drop Rotten Flesh (appended item id 34, procedural lump icon).
- Water flow uses appended ids 24-31. Chunk integration seeds only flowing
  cells, not generated source water, so pristine lake chunks stay byte-stable.
- Shore exit is sustained `SHORE_HOP = 5.5` while jump + wall + water applies.
  Do not restore the earlier launchy impulse.
- Inventory is 4x9, player save is `MCPL` v4, and furnace facing/lit variants
  are appended block ids 17-23.

## Verification

Recent Windows/MSVC work built warning-free and `world_tests` passed (run 3x
after the combat batch). Added coverage includes asset manifest/model loading,
external PNG texture loading, asset-manager caching, living
spawn/tick/freeze/collision/damage/drop/query behavior, deterministic ambient
creature spawning, stream-event duplicate prevention, movement-facing, MCEN
persistence/corruption/isolation including living records and the ambient
marker (roundtrip, kill + reload stays empty, fresh-session marker respect,
marker-only files kept), entity unload/load and autosave, demo save isolation,
player save v5 roundtrip + v4 full-health migration + clamping, player
damage/i-frames/regen/grace, hostile chase/attack/dead-player-ignored, melee
raycast hit/miss/range + knockback, water
spread/drain/infinite/drop-seek/save/shore hop, and
inventory/progression/furnace/UI migrations. Hearts HUD + creature visuals
inspected via `--demo-creature --demo-survival --frames 300` screenshot.
Latest TSAN was not run (no new threading surface: entities and saves remain
main-thread-only).

## Pointers

Verification recipes: `docs/handoff/VERIFICATION.md`. Roadmap: `ROADMAP.md`.
Completed-goal history: `docs/goals/`. Transient ignored state: `build/`,
`saves/`, `screenshot.*`, `settings.cfg`. World seed: 1337.
