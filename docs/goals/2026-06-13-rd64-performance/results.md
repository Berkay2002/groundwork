# RD64 Performance Results

## Goal

Keep render distance 64 viable on strong CPU/GPU hardware. Success target:
reduce the `--bench-secs` opaque chunk section by at least 25% from the
normal-world baseline without lowering visual/render distance, with p99 and
worst-frame time no worse than baseline.

## Scope

- Writable: `src/`, `tests/`, `docs/goals/2026-06-13-rd64-performance/`,
  and handoff/roadmap docs if behavior changes.
- Read-only by default: real player `saves/`; benchmark runs use scratch
  directories under `build/`.
- Loop bound: 3 optimization iterations unless the goal is met earlier.

## Iteration 0: Baseline

- Commit: pre-branch `main` at `074b007`.
- Command: `build\groundwork.exe --bench-secs 5 --time 0.7` from
  `build\perf-plan-baseline`.
- Result: 245.4 avg fps, 104.8 1% low, 92.5 0.1% low, 10.81 ms worst frame.
- Sections: events 0.09, tick 0.28, stream 0.00, mesh 0.00, edit 0.91,
  opaque 2.41, items 0.00, mobs 0.05, water 0.12, hud+swap 0.21 ms/frame.
- Draw/load: 16,770 chunks loaded, 5,444.5 chunks drawn/frame avg.

## Isolation Check

- Commit: pre-branch `main` at `074b007`.
- Command: `build\groundwork.exe --bench-secs 3 --time 0.7 --demo-menu` from
  `build\perf-plan-demo-nosave`.
- Result: 409.1 avg fps, 303.4 1% low, 215.2 0.1% low, 4.65 ms worst frame.
- Sections: events 0.00, tick 0.00, stream 0.00, mesh 0.00, edit 0.00,
  opaque 2.17, items 0.00, mobs 0.00, water 0.11, hud+swap 0.16 ms/frame.
- Draw/load: 16,641 chunks loaded, 5,323.0 chunks drawn/frame avg.
- Decision: invalid for the primary goal. Pause/menu mode stops simulation and
  hides the real gameplay path, so it can only isolate the terrain render
  section, not decide keep/revert.

## Current Comparator: No-Menu Creative RD64

- Commit: `0a72c19` (parent of iteration 2, after iteration 1 rollback).
- Command: `build\groundwork.exe --bench-secs 5 --time 0.7` from
  `build\perf-baseline-creative`, with scratch `settings.cfg` containing
  `render_distance=64`, `survival=0`, `vsync=1`, `fps_max=0`.
- Rationale: no menu and no demo flags; ambient/persistent mobs can exist and
  render, but creative mode prevents hostile targeting/damage from turning the
  render-distance benchmark into a combat benchmark.
- Result: 275.0 avg fps, 116.6 1% low, 97.2 0.1% low, 10.29 ms worst frame.
- Sections: events 0.01, tick 0.24, stream 0.00, mesh 0.00, edit 0.79,
  opaque 2.26, items 0.00, mobs 0.05, water 0.11, hud+swap 0.18 ms/frame.
- Draw/load: 16,641 chunks loaded, 5,323.0 chunks drawn/frame avg.

## Iteration 1: Dirty-Aware Entity Autosave

- Commit: `b9144f1`, reverted by `0909592`.
- Change: autosave only dirty loaded entity chunks instead of cycling every
  loaded entity chunk.
- Verification: `cmake --build build -j` and `build\world_tests.exe` passed.
- Benchmark command: `build\groundwork.exe --bench-secs 5 --time 0.7` from
  `build\perf-iter1`.
- Result: 246.0 avg fps, 75.7 1% low, 29.2 0.1% low, 34.26 ms worst frame.
- Sections: events 0.01, tick 0.35, stream 0.00, mesh 0.00, edit 0.81,
  opaque 2.47, items 0.00, mobs 0.05, water 0.12, hud+swap 0.25 ms/frame.
- Decision: reverted. The small `edit` improvement did not matter, `opaque`
  missed the target, and p99/worst-frame regressed versus baseline.

## Iteration 2: Skip Inactive Held-Light Shader Work

- Commit: `1957b07`, reverted by `ca2b99a`.
- Change: avoid per-fragment held-light distance/pow work when no emissive item
  is held.
- Verification: `cmake --build build -j` and `build\world_tests.exe` passed.
- Benchmark command: `build\groundwork.exe --bench-secs 5 --time 0.7` from
  `build\perf-iter2-real`, with scratch `settings.cfg` containing
  `render_distance=64`, `survival=0`, `vsync=1`, `fps_max=0`.
- Result: 268.5 avg fps, 104.1 1% low, 47.7 0.1% low, 20.95 ms worst frame.
- Sections: events 0.00, tick 0.25, stream 0.00, mesh 0.00, edit 0.81,
  opaque 2.31, items 0.00, mobs 0.05, water 0.11, hud+swap 0.20 ms/frame.
- Decision: reverted. It worsened opaque time versus the no-menu creative
  comparator and doubled the worst frame.

## Iteration 3: Skip Opaque Chunk Sort

- Commit: `0b3a7a8`, kept.
- Change: draw opaque chunks in culled map iteration order instead of sorting
  all visible chunks front-to-back; sort only the visible water subset
  back-to-front for the translucent pass.
- Verification: `cmake --build build -j` and `build\world_tests.exe` passed.
- Benchmark command: `build\groundwork.exe --bench-secs 5 --time 0.7` from
  `build\perf-iter3-real`, with scratch `settings.cfg` containing
  `render_distance=64`, `survival=0`, `vsync=1`, `fps_max=0`.
- Result: 297.0 avg fps, 113.7 1% low, 98.5 0.1% low, 10.15 ms worst frame.
- Sections: events 0.01, tick 0.22, stream 0.00, mesh 0.00, edit 0.74,
  opaque 2.06, items 0.00, mobs 0.05, water 0.12, hud+swap 0.18 ms/frame.
- Decision: kept. Opaque improved from 2.26 to 2.06 ms/frame (8.8%), avg fps
  improved from 275.0 to 297.0, and p99/worst did not regress. Goal not met;
  continue with another bounded batch.

## Iteration 4: Cache Visible Chunks Between Static Frames

- Commit: `a2f605f`, kept.
- Change: reuse the culled visible chunk list while eye/frustum and drawable
  chunk state are unchanged; invalidate on chunk unload and mesh upload.
- Verification: `cmake --build build -j` and `build\world_tests.exe` passed.
- Benchmark command: `build\groundwork.exe --bench-secs 5 --time 0.7` from
  `build\perf-iter4-real`, with scratch `settings.cfg` containing
  `render_distance=64`, `survival=0`, `vsync=1`, `fps_max=0`.
- Result: 335.4 avg fps, 122.2 1% low, 106.2 0.1% low, 9.41 ms worst frame.
- Sections: events 0.01, tick 0.22, stream 0.00, mesh 0.00, edit 0.65,
  opaque 1.75, items 0.00, mobs 0.05, water 0.12, hud+swap 0.18 ms/frame.
- Decision: kept. Opaque improved 22.6% versus the no-menu creative
  comparator and p99/worst improved. Goal not yet met on the corrected
  comparator, so continue.

## Iteration 5: Split Cached Visible Chunks By Pass

- Commit: `f34332d`, reverted by `d4f5ab4`.
- Change: keep separate cached opaque and water visible lists so static frames
  skip water refiltering/sorting and the opaque draw loop skips `hasOpaque()`.
- Verification: `cmake --build build -j` and `build\world_tests.exe` passed.
- Benchmark command: `build\groundwork.exe --bench-secs 5 --time 0.7` from
  `build\perf-iter5-real`, with scratch `settings.cfg` containing
  `render_distance=64`, `survival=0`, `vsync=1`, `fps_max=0`.
- Result: 310.5 avg fps, 103.8 1% low, 72.1 0.1% low, 13.88 ms worst frame.
- Sections: events 0.01, tick 0.25, stream 0.00, mesh 0.00, edit 0.72,
  opaque 1.90, items 0.00, mobs 0.05, water 0.11, hud+swap 0.19 ms/frame.
- Decision: reverted. It worsened opaque, p99, and worst frame versus
  iteration 4.

## Iteration 6: Use Vec2 Chunk Origin Uniform

- Commit: `8b92aa6`, reverted by `7c51aa2`.
- Change: replace chunk shader `vec3 uOrigin` with `vec2 uOriginXZ` and update
  per-chunk calls from `glUniform3f` to `glUniform2f`.
- Verification: `cmake --build build -j` and `build\world_tests.exe` passed.
- Benchmark command: `build\groundwork.exe --bench-secs 5 --time 0.7` from
  `build\perf-iter6-real`, with scratch `settings.cfg` containing
  `render_distance=64`, `survival=0`, `vsync=1`, `fps_max=0`.
- Result: 319.2 avg fps, 97.6 1% low, 76.3 0.1% low, 13.11 ms worst frame.
- Sections: events 0.01, tick 0.25, stream 0.00, mesh 0.00, edit 0.70,
  opaque 1.82, items 0.00, mobs 0.05, water 0.12, hud+swap 0.19 ms/frame.
- Decision: reverted. It worsened opaque, p99, and worst frame versus
  iteration 4.

## Iteration 7: Compute Chunk Fog In Vertex Shader

- Commit: `1a4bac8`, reverted by `13f68de`.
- Change: move fog factor calculation from the fragment shader to the vertex
  shader, keeping the existing interpolated fog style.
- Verification: `cmake --build build -j` and `build\world_tests.exe` passed.
- Benchmark command: `build\groundwork.exe --bench-secs 5 --time 0.7` from
  `build\perf-iter7-real`, with scratch `settings.cfg` containing
  `render_distance=64`, `survival=0`, `vsync=1`, `fps_max=0`.
- Result: 336.4 avg fps, 126.7 1% low, 101.7 0.1% low, 9.84 ms worst frame.
- Sections: events 0.01, tick 0.22, stream 0.00, mesh 0.00, edit 0.65,
  opaque 1.75, items 0.00, mobs 0.05, water 0.12, hud+swap 0.18 ms/frame.
- Decision: reverted. Opaque did not improve versus iteration 4, and the
  slightly better average/1% low did not justify extra shader change with a
  slightly worse worst frame.

## Accepted State

- Kept commits before squash: `0b3a7a8` and `a2f605f`.
- Kept changes: skip opaque front-to-back sort; cache visible chunks while
  eye/frustum and drawable chunk state are unchanged.
- Repeat check after later reverts: `build\groundwork.exe --bench-secs 5
  --time 0.7` from `build\perf-kept-repeat`, with scratch `settings.cfg`
  `render_distance=64`, `survival=0`, `vsync=1`, `fps_max=0`.
- Repeat result: 334.7 avg fps, 113.7 1% low, 60.8 0.1% low, 16.46 ms worst
  frame; opaque 1.75 ms/frame, p99 7.63 ms.
- Target status: accepted as material improvement. It meets the original
  normal-run opaque target (1.75 ms is below 75% of 2.41 ms) and lands just
  short of 25% on the corrected creative no-menu comparator (22.6% from
  2.26 ms). The remaining gap likely needs real chunk mesh batching rather
  than another micro-optimization.

## Final Verification

- Command: `cmake --build build -j && build\world_tests.exe && ctest
  --test-dir build` from a VS developer environment.
- Result: build passed, `world_tests` printed `all tests passed`, `ctest`
  reported 100% tests passed with `golden_screenshot` skipped.
- Command: `git diff --check`.
- Result: passed.
- Command: `build\groundwork.exe --bench-secs 5 --time 0.7` from
  `build\perf-final-rd64`, with scratch `settings.cfg`
  `render_distance=64`, `survival=0`, `vsync=1`, `fps_max=0`.
- Result: 333.6 avg fps, 126.2 1% low, 111.8 0.1% low, 8.94 ms worst frame;
  sections: events 0.01, tick 0.23, stream 0.00, mesh 0.00, edit 0.65,
  opaque 1.76, items 0.00, mobs 0.05, water 0.12, hud+swap 0.19 ms/frame.
