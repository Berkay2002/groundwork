# Roadmap

This roadmap is meant to be readable without knowing the codebase. It says what
the game can do today, what large areas are likely to come next, and what is
deliberately being left for later.

## What is already playable

- Explore a generated block world with hills, plains, trees, sand, lakes,
  caves, ores, bedrock, torches, and flowing water.
- Break and place blocks with a hotbar, inventory, crafting screens, furnaces,
  survival mode, tool tiers, durability, mining times, item counts, and item
  drops.
- Save and resume player position, inventory, world seed, time of day, modified
  chunks, furnace contents, and dropped item entities. Save files use versioned
  headers and temporary-file replacement so a crash during saving is less
  likely to damage real player data.
- Change common settings through `settings.cfg` or the pause menu: render
  distance, field of view, sensitivity, volume, vsync, key bindings, and
  survival or creative mode.
- Use a Minecraft-style UI: hotbar, 9-column inventory, crafting table, furnace
  screen, pause/settings menu, debug overlay, item icons, mining cracks, and
  first-person held items.
- See lighting, fog, day/night, water surfaces, smoother terrain shading, basic
  audio, item pickup, generated or embedded voxel/UI art, and the first authored
  blocky character asset loaded from `assets/`.
- Encounter hostile zombies in normal worlds and in a save-isolated demo. They
  wander, chase a visible player while actually facing them (smooth
  Minecraft-style body turns), flash red when hit, and bite; the player can
  fight back with a 3.5-block melee swing, knockback works both ways, and
  kills drop rotten flesh. Creatures persist across quitting and chunk
  streaming; each chunk ambient-spawns its creature exactly once per world,
  and new zombies keep appearing naturally at night or in dark places,
  24-96 blocks away, under a population cap.
- Survive with 20 HP of health: a hearts bar above the hotbar, brief
  invulnerability after a hit, slow passive regeneration, and death that
  respawns at world spawn. A keep-inventory setting (default on) decides
  whether death keeps items or spills them at the death point. Creative mode
  is invulnerable and ignored by hostiles.
- Run benchmark and screenshot checks for performance and visual regressions,
  plus headless world tests for core game logic.

## Promises to keep

- Existing saves are real player data. Changes should migrate safely, reject bad
  data clearly, or leave old data alone.
- Terrain generation should stay repeatable from world seed and coordinates, so
  the same unexplored place is generated the same way later.
- Windows and Linux should remain first-class targets.
- The build should stay warning-free, and world logic should stay covered by the
  headless test binary.
- Procedural/embedded content should stay procedural where it fits. Authored
  characters, mobs, props, structures, music, and similar content may use
  runtime assets, with manifest ids and source/license notes rather than
  hardcoded one-off paths.

## Next roadmap areas

### Save dropped items (done)

- Dropped items survive quitting, loading, and chunk streaming.
- Item entities save in optional chunk-scoped files under
  `saves/world1/entities/e_<cx>_<cz>.bin`.
- Items tick only while their chunk is active, matching Minecraft-style chunk
  ticking; despawn cleanup runs when those chunks are active again.
- The `MCEN` entity-file format is typed so future world entities can share
  the same persistence layer.

### Add simple creatures (combat + spawning passes done)

- Done: living-entity foundation (health, damage, deterministic ambient
  spawning, terrain collision, model id, rendered GLB character), creature
  persistence in `MCEN` files with a once-per-world ambient-spawn marker,
  hostile chase/attack AI with line of sight and correct target facing,
  player health/death/respawn with a keep-inventory toggle, two-way melee
  with knockback and a red hurt flash, a rotten-flesh drop, a data-driven
  mob table (`MobKind`/`MobDef`) with per-mob spawn reasons, and a
  Minecraft-style natural spawn system (night/darkness ring around the
  player with a population cap) so the world no longer depopulates.
- Next: more species and drop tables, despawn rules for far-away natural
  mobs, difficulty settings, or real pathfinding. Keep behavior modest until
  the game clearly needs more.

### Expand crafting and recipes

- Add more recipes and survival progression once there are enough blocks, mobs,
  or structures to justify them.
- Move stable recipe data out of C++ when the format is unlikely to churn every
  session.
- Improve recipe browsing so players can understand what they can make.

### Add more world variety

- Make exploration less repetitive with more terrain features, surface details,
  resources, or landmarks.
- Preserve repeatable generation from seed and coordinates.
- Do not treat this as an automatic commitment to biomes, weather, or a taller
  world.

### Add structures

- Add places worth finding, with clear placement rules and safe save/edit
  behavior.
- Structures should feel authored without breaking the deterministic world.

### Add chests and containers

- Let players store items in the world.
- Use containers to firm up block-attached state before adding many more
  stateful blocks.

### Introduce general block state

- Support saveable block details such as orientation, open or closed state,
  growth, variants, or container contents.
- Add this when concrete gameplay needs it, not as a framework for its own
  sake.

### Improve interaction feel

- Tune breaking, placing, swimming, selection, sounds, animations, and held-item
  behavior.
- Prefer small changes that make actions clearer and more deliberate.

### Add world selection

- Replace the fixed `saves/world1` path with named worlds and world metadata.
- Support creating, loading, and safely managing more than one world.
- Keep save safety more important than menu polish.

### Clean up render distance

- Make the render-distance setting match what the camera can actually show.
- Decide between a higher far plane, a setting cap, or separate load and draw
  distances.
- Verify the choice with screenshots and benchmark numbers.

### Move stable data out of C++

- Started with `assets/manifest.json` for authored model ids. Continue with
  low-risk data such as recipes, tuning values, or simple tables.
- Delay full modding, custom blocks, and scripting until the core rules have
  settled.

## Deliberately later

- Multiplayer remains out of scope for now. It would need world authority,
  chunk syncing, entity replication, prediction, reconciliation, save ownership,
  and protocol design.
- Real shadow mapping, full weather, complex biomes, and taller or infinite
  vertical worlds are still interesting, but they are not core-loop work yet.
- Revisit those after survival progression, containers, structures, and simple
  creatures have shown what the game actually needs.
