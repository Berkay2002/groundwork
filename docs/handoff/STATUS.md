# Status

Last updated: 2026-06-13, after the mob foundation pass: facing fix, MobKind
table, spawn reasons, natural night spawning, and the hurt flash.

Groundwork is past MVP. The completed history is in `docs/goals/`; future work
is organized by roadmap area in `ROADMAP.md`. Do not start roadmap work without
user approval; follow `docs/handoff/WORKFLOW.md`.

## Current Contract

- C++17/OpenGL 3.3 voxel game with streaming chunks, background generation and
  meshing, saves, lighting, water, caves, ores, survival progression, audio,
  menus, day/night, runtime character assets, deterministic ambient creature
  spawning, persistent hostile creatures, natural mob respawn pacing, player
  health with two-way melee combat, Windows support, and demo flags.
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
- Mobs are table-driven, not subclassed: `sim/Mob.h` holds `MobKind` (saved
  byte, append-only) and `MobDef` (model, max health, hostile, drop). Every
  mob also carries a `SpawnReason` (saved byte, append-only): Staged (0,
  demos/tests), Ambient (1), Natural (2). Living records save as MCEN type 4
  ("living v2"): the type-2 layout with the old ambient u8 reinterpreted as
  the reason (0/1 map exactly onto Staged/Ambient) plus a trailing kind byte
  after the model id. Type 2 stays loadable (kind defaults to Zombie); the
  writer always emits type 4.
- Natural spawning lives in `sim/MobSpawn.{h,cpp}` (MobSpawnSystem), NOT in
  Entities::tick. One attempt per tick, gated by main on survival + live
  player + not a demo. Rules: ring 24-96 blocks from the player (3D min),
  loaded chunk, cap 10 living within 128 blocks, solid non-water ground with
  two clear blocks above, and for hostiles block light <= 3 AND (night via
  dayFactor < 0.05, or sunlight <= 3 so caves spawn by day). Natural mobs
  persist like everything else; the once-per-world ambient marker is separate.
- Mob facing: `facingYaw = atan2(z, x)` in sim space, and the model rotation
  is **minus** facingYaw (positive GL Y-rotation turns +X toward -Z — getting
  this backwards mirrors facing and reads as walking sideways). Per-model
  orientation comes from `forwardYawDeg` in assets/manifest.json (Kenney
  characters face +Z, so 90). Chasing mobs face the player directly —
  including during knockback and attacks — with a ~10 deg/tick turn cap;
  wandering mobs face their velocity.
- Mobs flash red while hurtTicks > 0 (uFlash uniform in ModelRenderer,
  0.55 blend toward dark red).
- Mob rendering is culled: skip beyond MOB_RENDER_DISTANCE (96, the spawn
  ring's far edge) and outside the frustum. Persistent mobs accumulate
  world-wide (~28% of explored chunks + natural spawns), so draw cost must
  track what is on screen, not the population — uncull and a 16k-chunk world
  pays ~11 ms/frame in mob draws.
- Entity autosave trickles: Entities::autosaveTick saves a few chunk files
  per frame (one full cycle per ~30 s, hard cap 8/frame) instead of the old
  every-30-s burst of all loaded entity files, which stalled frames for
  seconds on big worlds. Exit and chunk-unload still do full synchronous
  saves. The F3 overlay shows live mob/item counts; --bench-secs has a
  dedicated "mobs" section for the living draw pass.
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
inventory/progression/furnace/UI migrations. New coverage: smoothed
wander/chase facing (turn cap, knockback gaze), MCEN living v2 records +
legacy type-2 mapping + bad kind/reason skipping, manifest forwardYawDeg
parse/validation, and MobSpawnSystem rules (darkness unit checks incl. sealed
pocket + torch, night spawn ring/footing/cap, zero day-surface spawns).
Facing verified visually from two demo screenshots against the saved camera
yaw (back at +Z walk, face at -X walk) and confirmed in user playtest. Hurt
flash is shader-side; the uFlash=0 path screenshot-checked, the flash itself
awaits playtest. Perf regression check ran in a scratch dir (`--bench-secs 10
--time 0.7`, 16k chunks, ~4.5k persistent mobs): mob culling + trickle
autosave took it from 33.5 avg fps / 6.5 s worst frame to 275 avg fps /
9.5 ms worst frame. Latest TSAN was not run (no new threading surface:
entities, spawning, and saves remain main-thread-only).

## Pointers

Verification recipes: `docs/handoff/VERIFICATION.md`. Roadmap: `ROADMAP.md`.
Completed-goal history: `docs/goals/`. Transient ignored state: `build/`,
`saves/`, `screenshot.*`, `settings.cfg`. World seed: 1337.
