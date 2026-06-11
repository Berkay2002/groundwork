#!/usr/bin/env python3
"""Golden-screenshot regression test.

Runs the game for a fixed number of frames from a fixed viewpoint in a fresh
temp world (fixed seed 1337 is compiled in), then compares the screenshot to
the checked-in reference image with a tolerance. Catches rendering
regressions (meshing, lighting, AO, atlas, shaders) mechanically.

Usage:
    golden_screenshot.py <minecraft-binary> <reference.png>            # check
    golden_screenshot.py --update <minecraft-binary> <reference.png>  # regen

Exit codes: 0 pass, 1 fail, 77 skipped (no display / no PIL).
The tolerance absorbs the varying debug-overlay text (fps/timings) and minor
GPU rasterization noise; real regressions move far more pixels than that.
"""
import os
import struct
import subprocess
import sys
import tempfile

FRAMES = 400
VIEW = (8.0, 52.0, 8.0, 45.0, -30.0)  # x y z yaw pitch (flying)
DIFF_THRESHOLD = 12      # per-pixel: max channel delta above this = "different"
MAX_DIFF_FRACTION = 0.015  # fail if more than 1.5% of pixels differ
MAX_MEAN_DIFF = 2.0      # fail if the average absolute delta drifts


def skip(msg):
    print(f"SKIP: {msg}")
    sys.exit(77)


def main():
    args = sys.argv[1:]
    update = "--update" in args
    args = [a for a in args if a != "--update"]
    if len(args) != 2:
        print(__doc__)
        sys.exit(1)
    binary, reference = os.path.abspath(args[0]), os.path.abspath(args[1])

    if not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY"):
        skip("no display available")
    try:
        from PIL import Image
    except ImportError:
        skip("PIL (python3-pil) not installed")

    with tempfile.TemporaryDirectory(prefix="mc_golden_") as tmp:
        os.makedirs(os.path.join(tmp, "saves", "world1"))
        # Pin the settings the reference was rendered with, so a change to
        # the game's *defaults* (e.g. render distance) can't break the test.
        with open(os.path.join(tmp, "settings.cfg"), "w") as f:
            f.write("render_distance=6\n")
        player = (b"MCPL" + struct.pack("<I", 1) + struct.pack("<5f", *VIEW)
                  + bytes([1, 0]))  # flying=1, hotbar slot 0
        with open(os.path.join(tmp, "saves", "world1", "player.bin"), "wb") as f:
            f.write(player)
        proc = subprocess.run([binary, "--frames", str(FRAMES)], cwd=tmp,
                              capture_output=True, text=True, timeout=120)
        shot_path = os.path.join(tmp, "screenshot.ppm")
        if proc.returncode != 0 or not os.path.exists(shot_path):
            print(proc.stdout + proc.stderr)
            print("FAIL: game run produced no screenshot")
            sys.exit(1)
        shot = Image.open(shot_path).convert("RGB")

        if update:
            shot.save(reference)
            print(f"reference updated: {reference} {shot.size}")
            return

        if not os.path.exists(reference):
            print(f"FAIL: reference image missing: {reference}\n"
                  f"Generate it with: {sys.argv[0]} --update {binary} {reference}")
            sys.exit(1)
        ref = Image.open(reference).convert("RGB")
        if ref.size != shot.size:
            print(f"FAIL: size mismatch ref {ref.size} vs shot {shot.size}")
            sys.exit(1)

        from PIL import ImageChops
        diff = ImageChops.difference(shot, ref)
        # Max channel delta per pixel, then count and average.
        maxdiff = diff.split()[0]
        for ch in diff.split()[1:]:
            maxdiff = ImageChops.lighter(maxdiff, ch)
        hist = maxdiff.histogram()
        total = shot.size[0] * shot.size[1]
        differing = sum(hist[DIFF_THRESHOLD + 1:])
        mean = sum(i * n for i, n in enumerate(hist)) / total
        frac = differing / total
        verdict = frac <= MAX_DIFF_FRACTION and mean <= MAX_MEAN_DIFF
        print(f"{'PASS' if verdict else 'FAIL'}: {frac * 100:.3f}% pixels differ "
              f"(limit {MAX_DIFF_FRACTION * 100:.1f}%), mean delta {mean:.3f} "
              f"(limit {MAX_MEAN_DIFF})")
        if not verdict:
            out = os.path.join(os.path.dirname(reference), "golden_failure.png")
            shot.save(out)
            print(f"actual screenshot saved for inspection: {out}")
            sys.exit(1)


if __name__ == "__main__":
    main()
