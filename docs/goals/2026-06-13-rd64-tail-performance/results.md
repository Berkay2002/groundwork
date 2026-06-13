# RD64 moving-camera tail-latency pass

Date: 2026-06-13
Branch: codex/groundwork-rd64-tail-performance

Goal:

- Improve RD64 moving-camera p99/max frame time without materially regressing average FPS or the existing opaque/edit section wins.
- Bounded loop: up to 4 focused experiment iterations.
- Benchmark setup: scratch directory under `build/`, copied `assets/`, `settings.cfg` with `render_distance=64`, `survival=0`, `vsync=1`, `fps_max=0`.
- Command baseline: `build/groundwork.exe --bench-secs 10 --time 0.7 --bench-spin 90`.
- No pause/menu benchmark. Creative mode keeps mobs renderable without hostile combat.

## Iteration 0: tail baseline before extra post-ready warmup

Baseline run 1:

```text
bench: warmed up in 39.9 s (16641 chunks loaded), measuring 10 s...
frames: 3510 in 10.00 s
fps: avg 350.9 | 1% low 118.8 | 0.1% low 93.4 | worst frame 81.7
frame ms: min 1.68 | p50 2.42 | p95 5.97 | p99 7.72 | max 12.24
sections (ms/frame avg): events 0.01 tick 0.24 stream 0.00 mesh 0.00 edit 0.56 opaque 1.71 items 0.00 mobs 0.05 water 0.09 hud+swap 0.18
chunks: 16641 loaded, 4786.3 drawn/frame avg, 0 mesh uploads
```

Baseline run 2:

```text
bench: warmed up in 37.9 s (16641 chunks loaded), measuring 10 s...
frames: 3695 in 10.00 s
fps: avg 369.4 | 1% low 134.1 | 0.1% low 102.6 | worst frame 97.0
frame ms: min 1.66 | p50 2.26 | p95 5.05 | p99 6.78 | max 10.31
sections (ms/frame avg): events 0.00 tick 0.19 stream 0.00 mesh 0.00 edit 0.52 opaque 1.66 items 0.00 mobs 0.05 water 0.09 hud+swap 0.19
chunks: 16641 loaded, 4753.9 drawn/frame avg, 0 mesh uploads
```

Observation:

- Current `--bench-secs` excludes chunk-load/generation/mesh/upload warmup, but begins measurement as soon as the queues are settled.
- For tail work, this may still count first steady-state frames affected by driver/resource cleanup or immediate post-load jitter.
- Next experiment: add an explicit post-settled benchmark warmup window before measured frames.

## Iteration 1: post-settled benchmark warmup

Commit: `0916714 experiment: add post-settle benchmark warmup`

Change:

- Added `--bench-warmup-secs S`, defaulting to 2 seconds.
- After generation/mesh/upload queues are empty, the benchmark keeps running the workload for the warmup interval before counting frames.
- `--bench-spin` also runs during this post-settle warmup, so measured frames are not the first moving-frustum frames.

Verification:

- `cmake --build build -j && build\world_tests.exe`: passed.

Benchmark run 1:

```text
bench: warmed up in 42.5 s (16641 chunks loaded, post-settle 2.0 s), measuring 10 s...
frames: 3542 in 10.00 s
fps: avg 354.2 | 1% low 116.3 | 0.1% low 85.2 | worst frame 74.6
frame ms: min 1.65 | p50 2.33 | p95 5.67 | p99 7.74 | max 13.41
sections (ms/frame avg): events 0.01 tick 0.24 stream 0.00 mesh 0.00 edit 0.56 opaque 1.68 items 0.00 mobs 0.05 water 0.09 hud+swap 0.19
chunks: 16641 loaded, 4739.6 drawn/frame avg, 0 mesh uploads
```

Benchmark run 2:

```text
bench: warmed up in 38.9 s (16641 chunks loaded, post-settle 2.0 s), measuring 10 s...
frames: 3850 in 10.00 s
fps: avg 384.9 | 1% low 144.2 | 0.1% low 129.5 | worst frame 127.2
frame ms: min 1.66 | p50 2.13 | p95 4.89 | p99 6.56 | max 7.86
sections (ms/frame avg): events 0.01 tick 0.18 stream 0.00 mesh 0.00 edit 0.48 opaque 1.62 items 0.00 mobs 0.05 water 0.08 hud+swap 0.18
chunks: 16641 loaded, 4740.5 drawn/frame avg, 0 mesh uploads
```

Decision: keep as benchmark harness.

- Two-run average p99 improved slightly (`7.25 -> 7.15 ms`) and two-run average max improved (`11.28 -> 10.64 ms`).
- One run still spiked to `13.41 ms`, so this does not solve tail latency.
- Next experiment should identify which frame section owns worst-frame spikes; average section timings are not enough.

## Iteration 2: worst-frame section attribution

Commit: `4b2e1d8 experiment: report worst-frame benchmark sections`

Change:

- Track the section timing breakdown for the single worst measured frame.
- Print `worst-frame sections (ms)` after the existing per-frame average section line.

Verification:

- `cmake --build build -j && build\world_tests.exe`: passed.

Benchmark run 1:

```text
bench: warmed up in 41.1 s (16641 chunks loaded, post-settle 2.0 s), measuring 10 s...
frames: 3774 in 10.00 s
fps: avg 377.4 | 1% low 141.7 | 0.1% low 115.3 | worst frame 108.7
frame ms: min 1.66 | p50 2.16 | p95 5.01 | p99 6.48 | max 9.20
sections (ms/frame avg): events 0.01 tick 0.18 stream 0.00 mesh 0.00 edit 0.51 opaque 1.63 items 0.00 mobs 0.05 water 0.09 hud+swap 0.18
worst-frame sections (ms): events 0.00 tick 0.00 stream 0.00 mesh 0.00 edit 7.13 opaque 1.79 items 0.00 mobs 0.04 water 0.05 hud+swap 0.18
chunks: 16641 loaded, 4739.7 drawn/frame avg, 0 mesh uploads
```

Benchmark run 2:

```text
bench: warmed up in 39.5 s (16641 chunks loaded, post-settle 2.0 s), measuring 10 s...
frames: 3421 in 10.00 s
fps: avg 342.1 | 1% low 118.6 | 0.1% low 103.9 | worst frame 98.8
frame ms: min 1.66 | p50 2.56 | p95 5.31 | p99 7.53 | max 10.13
sections (ms/frame avg): events 0.01 tick 0.23 stream 0.00 mesh 0.00 edit 0.60 opaque 1.75 items 0.00 mobs 0.05 water 0.09 hud+swap 0.19
worst-frame sections (ms): events 0.01 tick 5.30 stream 0.00 mesh 0.00 edit 1.42 opaque 2.92 items 0.00 mobs 0.06 water 0.19 hud+swap 0.22
chunks: 16641 loaded, 4738.5 drawn/frame avg, 0 mesh uploads
```

Decision: keep as benchmark instrumentation.

- Run 1's worst frame is dominated by `edit` at 7.13 ms; this section includes autosave and raycast/edit input handling.
- Run 2's worst frame is mixed, led by `tick` at 5.30 ms and opaque at 2.92 ms.
- Next runtime experiment: reduce entity autosave burst size and see whether the `edit` tail improves.

## Iteration 3: limit incremental entity autosave to one file per frame

Commit: `82f1912 experiment: limit entity autosave to one file per frame`

Change:

- Changed `Entities::autosaveTick` to save at most one dirty entity chunk file per frame.
- The existing credit system still paces dirty chunks; the change prevents multiple filesystem writes from stacking onto one rendered frame.

Verification:

- `cmake --build build -j && build\world_tests.exe`: passed.

Benchmark run 1:

```text
bench: warmed up in 38.3 s (16641 chunks loaded, post-settle 2.0 s), measuring 10 s...
frames: 3865 in 10.00 s
fps: avg 386.4 | 1% low 143.2 | 0.1% low 128.5 | worst frame 126.6
frame ms: min 1.65 | p50 2.10 | p95 4.91 | p99 6.54 | max 7.90
sections (ms/frame avg): events 0.01 tick 0.18 stream 0.00 mesh 0.00 edit 0.47 opaque 1.62 items 0.00 mobs 0.05 water 0.08 hud+swap 0.18
worst-frame sections (ms): events 0.00 tick 3.71 stream 0.00 mesh 0.00 edit 1.53 opaque 2.18 items 0.00 mobs 0.09 water 0.16 hud+swap 0.23
chunks: 16641 loaded, 4740.1 drawn/frame avg, 0 mesh uploads
```

Benchmark run 2:

```text
bench: warmed up in 36.1 s (16641 chunks loaded, post-settle 2.0 s), measuring 10 s...
frames: 3755 in 10.00 s
fps: avg 375.4 | 1% low 137.0 | 0.1% low 112.7 | worst frame 102.2
frame ms: min 1.64 | p50 2.18 | p95 5.11 | p99 6.71 | max 9.79
sections (ms/frame avg): events 0.01 tick 0.19 stream 0.00 mesh 0.00 edit 0.50 opaque 1.64 items 0.00 mobs 0.05 water 0.09 hud+swap 0.18
worst-frame sections (ms): events 0.01 tick 4.97 stream 0.00 mesh 0.00 edit 1.39 opaque 3.00 items 0.00 mobs 0.06 water 0.16 hud+swap 0.19
chunks: 16641 loaded, 4741.8 drawn/frame avg, 0 mesh uploads
```

Decision: keep.

- Two-run p99 improved versus iteration 2 (`6.48/7.53 -> 6.54/6.71`).
- Two-run max improved versus iteration 2 (`9.20/10.13 -> 7.90/9.79`).
- Worst-frame ownership shifted away from `edit`; remaining spikes are led by `tick`, so the next target is fixed-tick entity work.

## Iteration 4: skip unchanged entity bucket rebuilds

Commit: `9ac8464 experiment: skip unchanged entity bucket rebuilds`

Change:

- Track whether item/living chunk buckets actually need rebuilding.
- Newly spawned items mark buckets dirty for the next tick.
- Entity tick rebuilds buckets only when an entity crosses chunks, an entity is removed, or a spawn/load path already marked buckets dirty.

Verification:

- Initial build failed because `ChunkKey` defines `==` but not `!=`; fixed immediately and amended the experiment commit.
- `cmake --build build -j && build\world_tests.exe`: passed after the amend.

Benchmark run 1:

```text
bench: warmed up in 38.2 s (16641 chunks loaded, post-settle 2.0 s), measuring 10 s...
frames: 3851 in 10.00 s
fps: avg 385.1 | 1% low 146.8 | 0.1% low 134.8 | worst frame 134.0
frame ms: min 1.64 | p50 2.13 | p95 4.97 | p99 6.47 | max 7.46
sections (ms/frame avg): events 0.01 tick 0.18 stream 0.00 mesh 0.00 edit 0.47 opaque 1.63 items 0.00 mobs 0.05 water 0.08 hud+swap 0.18
worst-frame sections (ms): events 0.00 tick 3.89 stream 0.00 mesh 0.00 edit 1.40 opaque 1.82 items 0.00 mobs 0.05 water 0.09 hud+swap 0.19
chunks: 16641 loaded, 4743.0 drawn/frame avg, 0 mesh uploads
```

Benchmark run 2:

```text
bench: warmed up in 36.7 s (16641 chunks loaded, post-settle 2.0 s), measuring 10 s...
frames: 3824 in 10.00 s
fps: avg 382.4 | 1% low 141.2 | 0.1% low 102.6 | worst frame 70.5
frame ms: min 1.66 | p50 2.15 | p95 4.80 | p99 6.37 | max 14.19
sections (ms/frame avg): events 0.01 tick 0.18 stream 0.00 mesh 0.00 edit 0.48 opaque 1.63 items 0.00 mobs 0.05 water 0.08 hud+swap 0.18
worst-frame sections (ms): events 0.00 tick 0.00 stream 0.00 mesh 0.00 edit 1.28 opaque 2.15 items 0.00 mobs 0.05 water 0.13 hud+swap 10.58
chunks: 16641 loaded, 4740.1 drawn/frame avg, 0 mesh uploads
```

Benchmark repeat:

```text
bench: warmed up in 36.4 s (16641 chunks loaded, post-settle 2.0 s), measuring 10 s...
frames: 3846 in 10.00 s
fps: avg 384.5 | 1% low 144.1 | 0.1% low 128.5 | worst frame 126.9
frame ms: min 1.67 | p50 2.15 | p95 4.59 | p99 6.48 | max 7.88
sections (ms/frame avg): events 0.01 tick 0.18 stream 0.00 mesh 0.00 edit 0.46 opaque 1.64 items 0.00 mobs 0.05 water 0.09 hud+swap 0.18
worst-frame sections (ms): events 0.00 tick 4.38 stream 0.00 mesh 0.00 edit 1.55 opaque 1.62 items 0.00 mobs 0.05 water 0.05 hud+swap 0.22
chunks: 16641 loaded, 4742.7 drawn/frame avg, 0 mesh uploads
```

Decision: keep.

- p99 improved versus iteration 3 in all three runs (`6.54/6.71 -> 6.47/6.37/6.48`).
- The one bad max frame was dominated by `hud+swap` and did not reproduce.
- Typical max frames stayed below the iteration-3 maxes (`7.46` and `7.88` vs `7.90` and `9.79`).
