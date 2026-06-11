# Verification recipes

## Standard loop (every change)

```sh
cmake --build build -j        # must be warning-free (-Wall)
./build/world_tests           # must print "all tests passed"
```

## Visual verification (any rendering/UI/terrain change)

```sh
rm -rf saves screenshot.*                      # only if saves/ is your own test state!
timeout 90 ./build/groundwork --frames 300      # auto-exits, writes screenshot.ppm
python3 -c "from PIL import Image; Image.open('screenshot.ppm').save('screenshot.png')"
```

Then Read the PNG to inspect it, and send it to the user with SendUserFile
when closing out a batch. Clean up `saves/` and `screenshot.*` afterwards —
but **never delete a saves/ directory the user has actually played in**.

## Screenshot from a chosen viewpoint

The game restores the player from `saves/world1/player.bin`; craft one to
position the camera (fly mode on so the player doesn't fall during the run):

```sh
mkdir -p saves/world1 && python3 -c "
import struct
# MCPL v1: pos(3f) yaw(f) pitch(f) flying(u8) slot(u8)
d = b'MCPL' + struct.pack('<I', 1) + struct.pack('<5f', 250.0, 60.0, 250.0, 40.0, -12.0) + bytes([1, 0])
open('saves/world1/player.bin','wb').write(d)"
timeout 90 ./build/groundwork --frames 600      # 600 frames lets chunks stream in
```

## Staged screenshot scenes (lighting, structures)

When raw terrain can't show a feature (e.g. torch light needs darkness),
build the scene headlessly: compile a one-off probe against
`src/{World,Chunk,Terrain}.cpp` (same g++ line as the TSAN recipe, minus
TSAN) that opens `World(1337, "saves/world1")`, edits blocks, calls
`saveAllModified()`, and prints light values / surface heights for camera
placement. Then craft `player.bin` and run `--frames`. Terrain is NOT flat:
a "sealed" carved room can breach the surface — build above ground from a
solid box (fill stone, carve interior) instead of carving downward blind.

Two caveats learned the hard way:
- The game window grabs real mouse/keyboard during a `--frames` run. If the
  user touches anything, the crafted viewpoint is ruined **and** the run
  overwrites `player.bin` on exit — re-craft it and re-run; warn the user
  to keep hands off for ~10 s.
- If the user's `saves/` exists, `mv` it aside first and restore it after
  (never delete it); the probe's saves are yours to clean up.

To inspect detail in a screenshot, crop+upscale with PIL
(`im.crop(...).resize(..., Image.NEAREST)`) before Reading it.

## Batch G demo flags (items, inventory)

Breaking blocks can't be scripted in a `--frames` run, so two debug flags
stage the Batch G features for screenshots:

```sh
./build/groundwork --demo-items --frames 300   # 3 item cubes 3m ahead of the viewpoint
./build/groundwork --demo-inv --frames 120     # survival + stocked inventory, UI open
```

Run both from a temp dir (fresh world spawns on land; the real save's
viewpoint may be over a lake where drops sink out of sight). For live
input tests, `xdotool` works — but `windowactivate` the window first and
use `mousemove --window <id> x y` (window coords); clicks silently go
nowhere without activation.

## Threading changes

Rerun `world_tests` several times (`for i in 1 2 3; do ./build/world_tests; done`)
and do a ThreadSanitizer pass. TSAN crashes at startup on this kernel
(6.17, ASLR vs TSAN shadow mapping) unless ASLR is disabled for the process:

```sh
g++ -std=c++17 -fsanitize=thread -g -O1 -DGL_GLEXT_PROTOTYPES \
    tests/test_world.cpp src/Chunk.cpp src/World.cpp src/Terrain.cpp \
    src/Player.cpp src/Physics.cpp src/Entity.cpp \
    -o /tmp/tsan_tests -lGL -lpthread
setarch $(uname -m) -R /tmp/tsan_tests        # <- the ASLR workaround
```

This works because the test sources never call GL at runtime (linking GL is
enough); `World`/`Chunk`/`Terrain` logic including the worker pool runs fully
headless.

## Terrain inspection without the game

`Terrain` compiles standalone — for heightmap/feature debugging, build a tiny
probe against `src/Terrain.cpp` (no GL define needed) and print `heightAt`
over a grid. This caught the `int()` truncation bias; a wide min/max scan
(±400, step 4) is how hill/sand ranges were validated (19..51 for seed 1337).
