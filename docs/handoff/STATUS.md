# Status (last updated: 2026-06-11)

## Where the project stands

All planned batches through C are **done and verified**:

| Milestone | Contents | State |
|---|---|---|
| MVP (Phases 1–2) | Chunked world, terrain, face-culled meshing, FPS player with collision, break/place via raycast, streaming, chunk saving | done |
| Batch A | On-screen debug overlay (font8x8), hotbar UI, player persistence, `settings.cfg`, versioned save headers | done |
| Batch B | Trees, Wood/Leaves/Sand/Bedrock blocks, two-scale terrain (plains + hill mask), sand basins, bedrock floor | done |
| Batch C | Frustum culling, background generation + meshing on a worker pool, perf counters in overlay | done |

## What's next

**Batch D — Lighting** is the agreed next step (see `TODO.md` for its full
checklist and design notes: 4-bit sun + block light, BFS propagation and
removal, cross-border relight, light baked into mesh verts, torch block).
Batches E–H follow in order D → E → F → G → H; the reasoning for that order
is at the bottom of `TODO.md`.

**Do not start a batch unsolicited** — see `WORKFLOW.md`.

## Environment facts

- Ubuntu 24.04, gcc 13, CMake 3.28, GLFW 3.3.10 + GLM as system packages,
  X11 display `:0` available (real session, no xvfb installed).
- Git repository (initialized by the user on 2026-06-11). `build/`, `saves/`,
  `screenshot.*`, and `settings.cfg` are gitignored transient state.
- World seed fixed at 1337 in `main.cpp`.
- Internet access works (font8x8 was fetched via curl).

## Recent verification snapshot

At Batch C completion: 120 fps at render distance 6, overlay showed
`drawn 64/169` chunks (frustum culling working), gen and mesh ~0.1 ms/chunk
on workers, queues empty at steady state. Tests pass repeatedly and under
ThreadSanitizer.
