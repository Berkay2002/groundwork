# Working Agreement

`ROADMAP.md` is the living product map. It is organized by playable state,
promises, and future areas.

## Work Loop

1. Start only user-approved work. The user may point at a roadmap area, a bug,
   a goal doc, or a concrete feature request. Do not choose the next roadmap
   area unsolicited.
2. Before changing code, inspect the relevant files and decide the smallest
   long-term-friendly scope that actually serves the requested feature.
3. If the work is larger than a narrow fix, create or update a goal/plan under
   `docs/goals/` and keep it current while working.
4. Finish a work item by:
   - making the build warning-free,
   - passing `world_tests`,
   - visually checking rendering changes with a `--frames` screenshot run,
   - updating `ROADMAP.md` when the playable state or future roadmap changed,
   - updating `README.md` when controls, settings, or user-facing behavior
     changed,
   - summarizing what changed and exactly what was verified.
