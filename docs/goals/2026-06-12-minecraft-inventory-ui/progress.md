# Progress: Minecraft-Style Inventory UI Panel

## Status

- **Current phase:** Plan (Phase 4)
- **Current milestone:** —
- **Last checkpoint:** spec approved

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

## Parked Minor issues

- (none yet)
