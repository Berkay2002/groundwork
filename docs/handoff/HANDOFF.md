# Handoff — resume here for Batch E and beyond

Written 2026-06-11 at the end of the Batch D (lighting) session.
Last commit: `75b1956` "Batch D: lighting — sun/block light, BFS relight, 3D torch" (main).

## Read these first, in order

1. `CLAUDE.md` — architecture, commands, save-format rules.
2. `docs/handoff/STATUS.md` — where the project stands; has a "Batch D
   implementation notes" section describing the new lighting engine.
3. `ROADMAP.md` — current completed-batch record and future batch list.
4. `docs/handoff/GOTCHAS.md` — now includes a Lighting section. Read it
   before touching light, meshing, or block predicates.
5. `docs/handoff/VERIFICATION.md` — now includes the staged-screenshot-scene
   recipe (headless probe + crafted player.bin) used to verify lighting.
6. `docs/handoff/WORKFLOW.md` — the batch loop. **Do not start Batch E
   unsolicited; the user approves each batch** (Batch D was pre-approved in
   the session prompt — that approval does not carry forward).

## Session context not captured elsewhere

- **The user watches the work and reacts to screenshots.** This session they
  rejected two torch designs (full glowing cube, then a "lantern" tile)
  before being satisfied by a real 3D post with a fitted selection outline.
  Expect visual-fidelity feedback mid-batch; treat it as in-scope for the
  batch and iterate on the screenshot until it actually looks right.
- The user plays the game between sessions: `saves/world1/` contained real
  data (2 chunks + player.bin, restored untouched). Always check timestamps
  and back `saves/` up before any test run.
- Mid-run user input once corrupted a `--frames` screenshot (window grabs the
  mouse). Recipe and warning are in VERIFICATION.md.
- There is no file-sending tool in this environment despite WORKFLOW.md
  mentioning SendUserFile — leave screenshots at repo root / `/tmp` and give
  the user the paths.
- Verification screenshots from this session: `/tmp/shot_torch_room.png`
  (torch in dark hut), `/tmp/shot_outdoor.png` (surface + canopy shade).
  `/tmp/light_scene.cpp` is the scene-probe example; both are disposable.

## Batch E pointers (when approved)

- **Caves**: 3D-noise carving must remain a pure function of (world coords,
  seed) and keep bedrock intact. Lighting already handles caves: the sun BFS
  spreads into openings and `computeInitialLight` runs per chunk — verify a
  cave mouth gets a sunlight gradient and torch light works at depth (good
  screenshot material; the staged-scene recipe applies).
- **Ores**: append blocks after `Torch = 8` (never renumber), new atlas
  tiles after index 9 in `Texture.cpp`/`Block.h::tileFor`.
- **Water** is the structurally hard one: it needs a translucent second mesh
  pass per chunk, and a decision for each predicate in the new matrix —
  `isSolid` / `isOpaque` / `isCollidable` (see GOTCHAS Lighting section),
  plus whether water attenuates light (Minecraft: sun loses levels passing
  through). Mesh rule: no face culling between water and air; water-water
  faces culled. Probably worth a short written plan before coding.
- **Taller world** (optional, only if caves feel cramped): bump
  `CHUNK_HEIGHT` ⇒ bump `CHUNK_VERSION`, old saves regenerate; warn the user
  their modified chunks would be lost before doing it.

## Suggested skills

- `verify` / `run` — for the build → `world_tests` → `--frames` screenshot
  loop on every change (VERIFICATION.md has the exact recipes).
- `verification-before-completion` — before declaring a batch done or
  committing; the batch has hard completion criteria in WORKFLOW.md.
- `writing-plans` — for Batch E's water work specifically (translucency
  touches the mesher, draw loop, and block predicates at once).
- `systematic-debugging` / `diagnose` — for any lighting-seam or threading
  bug; pair with the TSAN recipe in VERIFICATION.md.
- `code-review` — worth a pass on the diff before the end-of-batch commit.
