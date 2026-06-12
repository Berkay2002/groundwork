# Progress: Minecraft-Style Inventory UI Panel

## Status

- **Current phase:** Execute (Phase 5)
- **Current milestone:** M2 (panel layout geometry)
- **Last checkpoint:** M1 reviewed and approved

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

## Parked Minor issues

- Q1 (M1): remap math `(i/8)*9 + i%8` duplicated in v2 and v3 load loops —
  extract `remapSlot8to9(i)` helper in PlayerSave.h. (cleanup pass)
- Q2 (M1): v2 migration test only populates row-0 source slots; add one
  row>0 fixture for symmetry with the v3 test. (cleanup pass)
