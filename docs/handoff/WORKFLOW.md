# Working agreement with the user

The user supplied a full MVP + growth roadmap (Phases 1–6) at project start;
`ROADMAP.md` is its distilled, living form.

## The batch loop

1. Work is scoped as **batches** in `ROADMAP.md` (A–H done; I onward planned).
2. The user **approves which batch to do** — ask before starting one, never
   pick one up unsolicited. Within an approved batch, work autonomously;
   don't ask permission for implementation details.
3. A batch is finished when:
   - the build is warning-free and `world_tests` passes,
   - the feature is verified visually via a `--frames` screenshot run,
   - `ROADMAP.md` is updated to mark the batch DONE and record what actually
     landed,
   - any per-batch plan/checklist is updated if one was created,
   - `README.md` is updated (controls table, Settings, How-it-works),
   - the user gets a screenshot via SendUserFile plus a summary that leads
     with what was added and how it was verified.

## Style and scope conventions

- Game-first, not engine-first: build systems when this game needs them
  (the user's "Practical Rule"). Resist speculative generality.
- No new dependencies without strong reason; no asset files (everything
  procedural/embedded); headless-testable logic stays GL-free.
- Tests: extend `tests/test_world.cpp` with plain CHECK functions for logic
  that can break silently (coordinate math, determinism, persistence,
  meshing rules). No test framework.
- Keep `docs/handoff/STATUS.md` current when finishing significant work.
