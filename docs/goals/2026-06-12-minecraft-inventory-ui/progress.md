# Progress: Minecraft-Style Inventory UI Panel

## Status

- **Current phase:** Execute (Phase 5)
- **Current milestone:** M3 (InventoryUi module + visuals)
- **Last checkpoint:** M2 reviewed and approved

## Log

### 2026-06-12 — Goal registered

- `goal.md` written and committed (`0dab9fd`). `.gitignore` exception added
  for the goal directory (existing per-goal pattern).

### 2026-06-12 — Spec approved

- `spec.md` written from the user-approved design (Approach B + 9-column
  migration).
- Spec review round 1 (general-purpose reviewer): REJECTED — 2 Important
  (S1 missing screenshot mechanism for crafting/furnace screens; S2
  contradictory uiSlotAt round-trip requirement on the furnace surface),
  6 Minor (S3 player-box height drift, S4 unapproved layout-file option,
  S5 dead shrink clause, S6 unverifiable signature clause, S7 v2-test
  wording contradiction, S8 stale comments uncovered).
- All 8 findings fixed in one batch (added --demo-craft/--demo-furnace
  flags, per-surface round-trip scoping, 3.5-slot player box, MenuUi.h
  pinned, definite fit-at-1280x720 rule, explicit contractual signature
  list, v2-test wording, comment updates folded into R1).
- Re-review (Sonnet 4.6): APPROVED. S1–S8 all RESOLVED, no regressions.
  One trivial FIRST_PASS_MISS logged: the loadPlayerFile version predicate
  `{1,2,3}` must widen to accept 4 — implicit in R1's "v4: read 36 slots
  directly"; carried as an implementation note, no spec change.
- Reviewer-model policy for this session (user instruction): Opus 4.8 for
  complex reviews (plan, milestone, final), Sonnet 4.6 for re-reviews/
  lighter checks.

### 2026-06-12 — Plan approved

- `plan.md` written: 3 milestones, 6 task contracts (M1: 9 cols + save v4,
  M2: panel geometry, M3: InventoryUi module + visuals + demo flags).
- Plan review round 1 (Opus 4.8): REJECTED — P1 Important (demo flags
  persist demo state into real `saves/world1`; fixed: mandatory save
  isolation skipping exit-path saves for all `--demo-*` runs + untouched-
  saves validation), P2 Important (v2/v3 fixtures/readers must pin a
  literal 32 source slots once SLOTS=36; fixed), P3–P5 Minor (ScreenKind
  enum pinned to InventoryUi.h, HotbarView field types pinned, furnace
  demo open-at-exact-position chain specified).
- Re-review (Sonnet 4.6): APPROVED, all findings resolved, no regressions.

### 2026-06-12 — M1/T1 DONE

- 9-column inventory (`COLS` 9, `SLOTS` 36), `Block::Planks` 9th palette
  entry, comments updated, tests updated (static_asserts + fill-all-36
  overflow). Build warning-free, `all tests passed`. Commit `0307a56`.

### 2026-06-12 — M1/T2 DONE

- Player save v4 (36 slots), version acceptance {1,2,3,4}, v2/v3 read
  paths pinned to literal 32 with r*8+c → r*9+c remap, fixtures pinned to
  literal 32, v3→v4 + v4 round-trip + updated v2 tests. Build warning-free,
  `all tests passed`. Commit `6245487`.

### 2026-06-12 — M1 milestone review APPROVED

- Fresh validation: build exit 0 warning-free, `all tests passed`.
- Stage 1 spec compliance (Opus 4.8): APPROVED, zero findings; verified
  literal-32 read paths/fixtures, r*9+c placement, col-8 coverage in the
  v4 round-trip, no scope creep.
- Stage 2 code quality (Opus 4.8): APPROVED; 2 Minor parked (below).

### 2026-06-12 — M2/T3 DONE + M2 milestone review APPROVED

- Panel layout geometry in MenuUi.h: centered panel, title band, top
  section per surface (player box + 2×2 + arrow + output / centered 3×3 /
  furnace stack), 3×9 grid, 0.45-slot hotbar gap, recipe panel attached
  right. Contractual signatures unchanged; new panelRect/recipePanelRect/
  playerBoxRect/arrowRect. `testMenuUiPanelLayout` added. Build warning-
  free, `all tests passed`. Commit `a2233b2`.
- Milestone review (Opus 4.8, combined single-task dispatch): Stage 1
  spec compliance APPROVED (zero findings, fits at 1280×720 verified
  numerically); Stage 2 quality APPROVED with 4 Minor parked (below).

### 2026-06-12 — M3 tasks T4–T6 + regression repair + user feedback fix

- T4 DONE (`53c95d9`): InventoryUi.{h,cpp} extraction — ScreenKind,
  InventoryView/HotbarView, drawItemStack/drawHotbar/drawInventoryScreen;
  main.cpp slimmed; CMake updated; saves/ backed up + restored during
  validation runs.
- T5 DONE (`94c227a`): drawPanel/drawBeveledSlot/drawArrow style helpers,
  Minecraft beveled-panel rendering for all three screens, count-text
  shadow; build warning-free, tests green.
- T6 reported DONE (`19cd690`) but **clobbered T4/T5's main.cpp** — the
  implementer worked from a stale pre-T4 main.cpp (zero module references
  at HEAD; controller caught it by inspecting the screenshots, which showed
  the old flat style despite the agent's claimed visual inspection).
  World.{h,cpp} setDemoMode, ROADMAP/STATUS updates from T6 were good.
- Repair DONE (`babed1c`): restored 94c227a main.cpp, re-applied T6's
  intended behavior (demoRun flag, autosave/exit-save gating, setDemoMode,
  --demo-craft, --demo-furnace). All three screenshots inspected by both
  the repair agent and the controller: new beveled style confirmed on
  inventory/crafting/furnace screens. saves/world1 verified untouched.
- User feedback mid-run: in-game HUD hotbar should not show brightly under
  the open inventory (Minecraft reference: it sits dimmed under the
  overlay). Root cause: Hud draws solids before tiles/text in one batch,
  so the dim solid rendered under the hotbar icons. Fix (`b72c9ef`):
  flush the HUD pass (end/begin) before drawing the overlay. Verified by
  fresh --demo-inv screenshot: hotbar now dimmed. Tests green,
  saves/world1 untouched.

### 2026-06-12 — M3 stage-1 review + user addendum batch (`4f47f9b`)

- M3 spec-compliance review (Opus 4.8): APPROVED. One Minor, C1: chunk
  eviction in World::update() called saveChunk() ungated by demoMode_.
- User addendum (mid-run, user-directed): 4 decorative armor placeholder
  slots left of the player box; panel midline as a divider — armor + player
  box in the left half, 2x2 craft cluster centered in the right half.
  Spec R2 amended. Implemented in MenuUi.h (armorSlotRect, topH = 4 slots,
  playerBoxRect repositioned/full-height, craftGridLeft midline-anchored)
  + InventoryUi.cpp (draw armor column). Furnace cluster now self-centered
  (also resolves parked Q3 (M2)). Layout tests extended: armor containment/
  disjointness/hit-test-none + midline-balance assertions.
- C1 fixed: saveChunk() itself now returns early under demoMode_.
- Validation: build warning-free, `all tests passed`, all three screens
  re-screenshotted and controller-inspected (armor column + balance
  confirmed; furnace centered; crafting table unchanged).
- Save-data note (honesty log): saves/world1 player.bin was rewritten at
  2026-06-12 2:29 PM as v4/210 bytes (plus level.bin/block_entities.bin
  refresh) — evidence points to the stage-1 reviewer running a bare
  --frames run, which saves by design for non-demo runs. Contents
  round-tripped through the v3→v4 migration (lossless); chunks untouched;
  re-tested demo + test binaries afterward: no writes to saves/world1.

### 2026-06-12 — M3 stage-2 quality review APPROVED + cleanup pass (`df17eb1`)

- M3 code-quality review (Opus 4.8): APPROVED. Verified amended-R2
  compliance for `4f47f9b` (armor decorative + midline balance + furnace
  self-centered). One Minor: M3Q1, level.bin written from the World
  constructor before setDemoMode() could gate it (fresh-machine seam).
- Cleanup pass (all parked Minors resolved in one batch): remapSlot8to9
  helper (M1 Q1); v2 test row-1 fixture (M1 Q2); recipe rects in the
  pairwise non-overlap sweep (M2 Q1); y0 derived via topSectionY (M2 Q2);
  arrowRect reads rects instead of re-deriving (M2 Q4); demo mode moved
  into the World constructor — `World(seed, dir, demoMode)`, setDemoMode()
  removed, constructor-time level.bin/create_directories gated (M3Q1).
  ROADMAP/STATUS corrected (module owns drawing only; layout/hit-testing
  stay in MenuUi.h).
- Validation: build warning-free, `all tests passed`, all three demo runs
  leave saves/world1 byte-identical (name+size+timestamp compare).
- Mid-validation interruption: a LNK1104 was caused by the user's own live
  play session holding groundwork.exe; user closed it, rebuild clean. The
  2:35–2:43 PM saves/world1 changes were the user's legitimate gameplay.

## Parked Minor issues

- (none — all resolved in `4f47f9b` / `df17eb1`)
