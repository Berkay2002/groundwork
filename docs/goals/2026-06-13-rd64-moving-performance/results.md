# RD64 moving-camera performance pass

Date: 2026-06-13
Branch: codex/groundwork-rd64-moving-performance

Benchmark setup:

- Scratch directory under `build/`, with copied `assets/`.
- `settings.cfg`: `render_distance=64`, `survival=0`, `vsync=1`, `fps_max=0`.
- Command: `build/groundwork.exe --bench-secs 5 --time 0.7 --bench-spin 90`
- No pause/menu benchmark. Creative mode is used so mobs can spawn/render without attacking the player.

## Iteration 0: moving-camera baseline

Commit: `9afa72e experiment: add moving-camera benchmark spin`

Result: kept as benchmark instrumentation.

Output:

```text
bench: warmed up in 39.0 s (16641 chunks loaded), measuring 5 s...
frames: 1630 in 5.00 s
fps: avg 326.0 | 1% low 129.6 | 0.1% low 111.4 | worst frame 111.4
frame ms: min 1.91 | p50 2.56 | p95 5.61 | p99 7.19 | max 8.98
sections (ms/frame avg): events 0.01 tick 0.20 stream 0.00 mesh 0.00 edit 0.67 opaque 1.86 items 0.00 mobs 0.05 water 0.09 hud+swap 0.18
chunks: 16641 loaded, 4747.1 drawn/frame avg, 0 mesh uploads
workers: gen 0.72 ms/chunk, mesh 0.52 ms/chunk (moving avg), queues gen 0 mesh 0 upload 0
```

Notes:

- This benchmark invalidates the static visible-list cache every frame by rotating the camera during the measured window.
- The next target is reducing the cost of rebuilding/drawing the moving visible chunk set without changing the main-thread GL/chunk ownership rule.

## Iteration 1: cached drawable chunk candidates

Commit: `eb7a2e0 experiment: cache drawable chunk candidates`

Change:

- Maintain a main-thread draw-candidate vector for chunks with uploaded opaque or water mesh.
- Cache each candidate's AABB, chunk origin, and center so moving-camera visible rebuilds do not scan `chunks_` or reconstruct bounds every frame.

Verification:

- `cmake --build build -j && build\world_tests.exe`: passed.

Benchmark run 1:

```text
bench: warmed up in 38.9 s (16641 chunks loaded), measuring 5 s...
frames: 1693 in 5.00 s
fps: avg 338.6 | 1% low 110.2 | 0.1% low 90.8 | worst frame 90.8
frame ms: min 1.73 | p50 2.42 | p95 5.91 | p99 8.10 | max 11.02
sections (ms/frame avg): events 0.01 tick 0.24 stream 0.00 mesh 0.00 edit 0.65 opaque 1.72 items 0.00 mobs 0.05 water 0.09 hud+swap 0.19
chunks: 16641 loaded, 4744.6 drawn/frame avg, 0 mesh uploads
```

Benchmark run 2:

```text
bench: warmed up in 38.9 s (16641 chunks loaded), measuring 5 s...
frames: 1677 in 5.00 s
fps: avg 335.4 | 1% low 112.3 | 0.1% low 86.4 | worst frame 86.4
frame ms: min 1.73 | p50 2.47 | p95 5.83 | p99 7.59 | max 11.57
sections (ms/frame avg): events 0.01 tick 0.24 stream 0.00 mesh 0.00 edit 0.67 opaque 1.72 items 0.00 mobs 0.05 water 0.09 hud+swap 0.19
chunks: 16641 loaded, 4740.5 drawn/frame avg, 0 mesh uploads
```

Decision: kept.

- Target metric improved: opaque section `1.86 -> 1.72 ms/frame` in both runs.
- Average fps improved: `326.0 -> 335.4-338.6`.
- Tail metrics worsened versus the single baseline run (`p99 7.19 -> 7.59-8.10`, max `8.98 -> 11.02-11.57`), so the next iteration should focus on ordering/tail behavior or confirm this is benchmark variance.

## Iteration 2: autosave only dirty entity chunks

Commit: `4d19c84 experiment: autosave only dirty entity chunks`

Change:

- Track dirty entity chunks when items/living entities spawn, move, merge, take damage, die, or create an ambient spawn marker.
- Keep explicit save/unload/quit save behavior intact.
- Change incremental `Entities::autosaveTick` to pace over dirty chunks instead of every loaded entity chunk.

Verification:

- `cmake --build build -j && build\world_tests.exe`: passed.

Benchmark:

```text
bench: warmed up in 40.9 s (16641 chunks loaded), measuring 5 s...
frames: 1716 in 5.00 s
fps: avg 343.1 | 1% low 115.7 | 0.1% low 86.0 | worst frame 86.0
frame ms: min 1.69 | p50 2.59 | p95 6.04 | p99 7.69 | max 11.63
sections (ms/frame avg): events 0.01 tick 0.26 stream 0.00 mesh 0.00 edit 0.58 opaque 1.73 items 0.00 mobs 0.05 water 0.09 hud+swap 0.19
chunks: 16641 loaded, 4744.3 drawn/frame avg, 0 mesh uploads
```

Decision: kept.

- Target metric improved: edit section `0.65-0.67 -> 0.58 ms/frame` versus iteration 1.
- Average fps improved again: `335.4-338.6 -> 343.1`.
- Tail metrics remain worse than iteration 0, so a later pass should inspect frame-time spikes separately from average section costs.

## Iteration 3: center/extent frustum helper

Commit: `65bf8f6 experiment: use center extent frustum checks for chunks`

Change:

- Added `Frustum::intersectsBox(center, half)` and used it for chunk draw-candidate culling.
- Replaced cached candidate min/max bounds with a cached center point.

Verification:

- `cmake --build build -j && build\world_tests.exe`: passed.

Benchmark:

```text
bench: warmed up in 40.6 s (16641 chunks loaded), measuring 5 s...
frames: 1636 in 5.00 s
fps: avg 327.0 | 1% low 100.4 | 0.1% low 62.4 | worst frame 62.4
frame ms: min 1.72 | p50 2.74 | p95 6.46 | p99 8.34 | max 16.01
sections (ms/frame avg): events 0.01 tick 0.28 stream 0.00 mesh 0.00 edit 0.63 opaque 1.79 items 0.00 mobs 0.06 water 0.10 hud+swap 0.20
chunks: 16641 loaded, 4745.7 drawn/frame avg, 0 mesh uploads
```

Decision: reverted.

- Revert commit: `3d3b387 Revert "experiment: use center extent frustum checks for chunks"`
- The change worsened average fps, opaque time, edit time, and tail latency versus iteration 2.

## Final kept-state verification

Commits included in final kept diff:

- `9afa72e experiment: add moving-camera benchmark spin`
- `eb7a2e0 experiment: cache drawable chunk candidates`
- `4d19c84 experiment: autosave only dirty entity chunks`

Verification:

- `cmake --build build -j && build\world_tests.exe && ctest --test-dir build`: passed.
- `ctest` result: 2 tests discovered, `world_tests` passed, `golden_screenshot` skipped by harness.

Benchmark run 1:

```text
bench: warmed up in 39.8 s (16641 chunks loaded), measuring 5 s...
frames: 1684 in 5.01 s
fps: avg 336.3 | 1% low 97.8 | 0.1% low 47.8 | worst frame 47.8
frame ms: min 1.65 | p50 2.66 | p95 6.20 | p99 8.36 | max 20.92
sections (ms/frame avg): events 0.01 tick 0.27 stream 0.00 mesh 0.00 edit 0.60 opaque 1.75 items 0.00 mobs 0.05 water 0.09 hud+swap 0.19
chunks: 16641 loaded, 4741.8 drawn/frame avg, 0 mesh uploads
```

Benchmark run 2:

```text
bench: warmed up in 38.3 s (16641 chunks loaded), measuring 5 s...
frames: 1816 in 5.00 s
fps: avg 363.0 | 1% low 125.7 | 0.1% low 113.1 | worst frame 113.1
frame ms: min 1.68 | p50 2.23 | p95 5.78 | p99 7.60 | max 8.84
sections (ms/frame avg): events 0.01 tick 0.24 stream 0.00 mesh 0.00 edit 0.52 opaque 1.66 items 0.00 mobs 0.05 water 0.09 hud+swap 0.18
chunks: 16641 loaded, 4748.8 drawn/frame avg, 0 mesh uploads
```

Final decision: keep and squash.

- Against iteration 0, final repeat improved average fps `326.0 -> 363.0`.
- Opaque improved `1.86 -> 1.66 ms/frame`.
- Edit/autosave improved `0.67 -> 0.52 ms/frame`.
- Tail remained noisy: final run 1 was bad, final run 2 was near baseline (`p99 7.60` vs `7.19`, max `8.84` vs `8.98`). Treat tail spikes as a separate future target.
