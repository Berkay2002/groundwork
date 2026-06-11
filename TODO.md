# Roadmap Checklist

MVP (Phases 1–2) is done: chunked world, terrain, meshing, walking/collision,
break/place, streaming, saving, debug-in-title.

## Batch A — Usable Game Foundation (Phase 3) — DONE

- [x] On-screen debug overlay (bitmap-font text rendering: FPS, pos, chunk,
      loaded chunks, rebuilds, targeted block) — replaces window-title HUD
- [x] Hotbar UI (textured slots at bottom, selected-slot highlight,
      number keys + scroll wheel to select)
- [x] Player data persistence (save/restore position, look direction, fly
      mode, selected slot in `saves/world1/player.bin`)
- [x] Settings file (`settings.cfg`: mouse sensitivity, FOV, render distance,
      vsync) loaded at startup
- [x] Save format versioning (magic + version header on chunk files and
      player file; reject/regenerate on mismatch)

## Batch B — Better Terrain (Phase 3) — DONE

- [x] Trees (deterministic placement from seed, trunk + leaf blocks →
      new block types: Wood, Leaves, Sand; bedrock is unbreakable)
- [x] Two-scale terrain: rolling plains + occasional hills/mountains
- [x] Bedrock layer at y=0

## Batch C — Scalable World (Phase 4) — DONE

- [x] Frustum culling (skip drawing chunks outside the camera frustum)
- [x] Background chunk generation + meshing on worker threads
      (main thread only uploads finished meshes; chunk lifecycle states)
- [x] Performance counters in debug overlay (mesh time, gen time, draw count)

## Batch D — Lighting (Phase 6 pulled forward: biggest visual win) — DONE

- [x] Per-block light storage in chunks (4-bit sunlight + 4-bit block light)
- [x] Sunlight: top-down column fill, then BFS spread into overhangs/caves
- [x] Incremental relight on block place/break (BFS add, unlight-BFS remove)
- [x] Cross-chunk-border propagation (relight neighbors, like mesh dirtying)
- [x] Bake light into mesh vertices; combine with existing face shading
- [x] Torch block (first light-emitting block, new atlas tile, hotbar slot;
      rendered as a 3D post, walk-through, doesn't block light)
- [x] Save light or recompute on load (decided: recompute — save format stays v1)

## Batch E — Underground & Richer Terrain (Phase 5)

- [ ] Caves (3D noise carving below the surface; keep bedrock floor intact)
- [ ] Ores (Coal/Iron blocks seeded deterministically in stone, depth-banded)
- [ ] Water as static block (translucent: needs a second mesh pass per chunk,
      no face culling between water and air, swim-through = non-solid)
- [ ] Taller world if caves feel cramped (CHUNK_HEIGHT bump → save version 2
      + migration/regeneration path)

## Batch F — Mesh & Render Optimization (Phase 4 leftovers)

- [ ] Greedy meshing (merge coplanar same-tile faces; needs texture wrap
      handling — switch atlas strip to a texture array or per-face UV tiling)
- [ ] Vertex size reduction (pack position/uv/light into ints; measure first
      via the existing perf counters)
- [ ] Sort chunk draws front-to-back for early-z; one persistent VAO layout
- [ ] Occlusion heuristics only if profiling shows draw-bound (don't guess)

## Batch G — Items & Inventory (Phase 5)

- [ ] Block drops: breaking yields an item (no entity physics at first —
      direct-to-inventory is fine)
- [ ] Item counts in hotbar slots + finite placement (survival-ish mode flag;
      keep current infinite mode as creative)
- [ ] Full inventory grid UI (open/close with E, move stacks, Hud-based)
- [ ] Inventory persistence in player.bin (bump player save to v2)

## Batch H — Polish & Distribution (Phase 3/“Build and Distribution”)

- [ ] Audio (block break/place, footsteps; miniaudio or OpenAL, keep optional)
- [ ] Pause menu (resume / settings / quit; settings editable in-game and
      written back to settings.cfg)
- [ ] Key rebinding via settings.cfg
- [ ] Day/night cycle (sky color + sun light level over time — cheap once
      Batch D lighting exists)
- [ ] Release build packaging (strip, assets-free binary, README quickstart;
      test build on a clean machine/container)

## Far later (only with a working foundation)

- [ ] Crafting (needs inventory from Batch G first)
- [ ] Mobs (pathing, spawning, combat — large)
- [ ] Multiplayer (world authority, prediction — very large; avoid baking in
      single-player assumptions meanwhile)
- [ ] Modding/scripting (stable data formats first; data-driven block/item
      definitions are the prerequisite step)

## Suggested order

D → E → F → G → H. Lighting first because every later screenshot benefits;
caves/water build on lighting (dark underground needs torches); greedy meshing
after water since translucent blocks change the meshing rules; inventory
before crafting; polish last so it polishes something complete.
