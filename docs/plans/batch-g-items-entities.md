# Batch G — Items, Inventory & Entity Foundation: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fixed-timestep simulation (20 TPS, interpolated rendering), a minimal entity system whose first entity is the dropped item (bobbing cube, magnetized pickup), survival mode with finite stacked items, a full inventory grid UI, and player-save v2 persistence.

**Architecture:** The player's per-axis sub-stepped AABB collision is extracted into a shared `Body`/`moveBody` in `src/Physics.{h,cpp}` so item entities reuse it verbatim. All simulation (player physics, entity ticks) moves into a 20 TPS accumulator loop in `main.cpp`; rendering interpolates positions by `alpha = accumulator / TICK_DT`. Entities are main-thread-only (like the chunk map) and live in an `Entities` manager with per-chunk buckets. Inventory logic (`src/Inventory.h`) and player persistence (`src/PlayerSave.h`) are GL-free and headless-testable. The inventory UI is built entirely from existing `Hud` primitives. Creative mode (current behavior: fixed palette, infinite blocks, no drops) stays the default; `survival=1` in `settings.cfg` enables drops, counts, finite placement, and the E-key inventory.

**Tech Stack:** C++17, OpenGL 3.3 (Mesa, `GL_GLEXT_PROTOTYPES`), GLFW, GLM. No new dependencies, no asset files. Tests stay plain-CHECK in `tests/test_world.cpp`.

**Mode rules (locked in):**
- Creative (default): fixed `HOTBAR[]` palette, infinite placement, breaking destroys with **no drop**, no inventory UI (E does nothing).
- Survival (`survival=1` in settings.cfg): breaking spawns the registry's `drop` as an item entity; placement consumes from the selected hotbar stack; hotbar shows inventory row 0 with counts; E opens the 4×8 grid.
- Item entities are **not persisted** (quit/unload loses them) and **freeze** (no physics, no aging) while their chunk is unloaded. Both are documented limitations, not bugs.
- Placing a block into the cell occupied by an item entity can entomb it (it stays frozen inside). Known minor issue, accepted for this batch.

---

### Task 1: Shared AABB physics (`Physics.{h,cpp}`), Player delegates to it

**Files:**
- Create: `src/Physics.h`, `src/Physics.cpp`
- Modify: `src/Player.h` (drop `moveAxis`, add `prevPos`/`beginTick`/`renderPos` — interpolation fields used by Task 2), `src/Player.cpp`
- Modify: `CMakeLists.txt` (add `src/Physics.cpp` to both targets; add `src/Player.cpp` to `world_tests`)
- Test: `tests/test_world.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_world.cpp` (and call from `main()`):

```cpp
#include "../src/Physics.h"
#include "../src/Player.h"

// Body physics on a hand-built platform high above terrain (y=70 is air
// everywhere near spawn: terrain tops out ~45 with trees).
static void testBodyPhysics() {
    std::filesystem::remove_all("test_phys_save");
    World w(1337, "test_phys_save");
    w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 1, 10000);
    w.setBlock(0, 70, 0, Block::Stone);

    // Falls and lands exactly on top of the platform.
    Body b;
    b.halfWidth = 0.125f;
    b.height = 0.25f;
    b.pos = glm::vec3(0.5f, 75.0f, 0.5f);
    for (int i = 0; i < 200 && !b.onGround; ++i) {
        b.vel.y += -22.0f * 0.05f;
        moveBody(w, b, 0.05f);
    }
    CHECK(b.onGround);
    CHECK(std::abs(b.pos.y - 71.0f) < 1e-3f);
    CHECK(b.vel.y == 0.0f);

    // Horizontal motion stops at a wall and zeroes that velocity component.
    w.setBlock(1, 71, 0, Block::Stone);
    b.vel = glm::vec3(4.0f, 0.0f, 0.0f);
    for (int i = 0; i < 40; ++i) moveBody(w, b, 0.05f);
    CHECK(b.pos.x <= 1.0f - 0.125f + 1e-4f);
    CHECK(b.vel.x == 0.0f);

    std::filesystem::remove_all("test_phys_save");
}

// The Player refactor onto Body must not change player physics.
static void testPlayerLandsOnPlatform() {
    std::filesystem::remove_all("test_player_save");
    World w(1337, "test_player_save");
    w.waitUntilLoaded(glm::vec3(8.5f, 50.0f, 8.5f), 1, 10000);
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz)
            w.setBlock(8 + dx, 70, 8 + dz, Block::Stone);
    Player p;
    p.pos = glm::vec3(9.0f, 75.0f, 9.0f);
    PlayerInput in;
    for (int i = 0; i < 200 && !p.onGround; ++i) p.update(w, in, 0.05f);
    CHECK(p.onGround);
    CHECK(std::abs(p.pos.y - 71.0f) < 1e-3f);
    std::filesystem::remove_all("test_player_save");
}
```

- [ ] **Step 2: Run to verify it fails to compile**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: FAIL — `Physics.h: No such file or directory` (after the CMake edit in step 3; order the edits so the test exists first).

- [ ] **Step 3: Implement `src/Physics.h`**

```cpp
#pragma once
#include <glm/glm.hpp>

class World;

// Axis-aligned physics body shared by the player and entities: pos is the
// feet center (center in X/Z, bottom in Y), exactly like the player always
// worked. Movement is per-axis and sub-stepped so nothing tunnels.
struct Body {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    float halfWidth = 0.3f;
    float height = 1.8f;
    bool onGround = false;
};

bool bodyCollidesAt(const World& world, const Body& b, const glm::vec3& p);
// Move along one axis in <=0.45-block steps; a collision zeroes that
// velocity component (and sets onGround when landing on Y).
void moveBodyAxis(const World& world, Body& b, int axis, float amount);
// Y first (stable ground detection), then X, Z. Clears onGround first.
void moveBody(const World& world, Body& b, float dt);
```

- [ ] **Step 4: Implement `src/Physics.cpp`** (this is the player's old `collidesAt`/`moveAxis`, verbatim, parameterized)

```cpp
#include "Physics.h"
#include "World.h"
#include <cmath>

bool bodyCollidesAt(const World& world, const Body& b, const glm::vec3& p) {
    const float hw = b.halfWidth;
    int x0 = (int)std::floor(p.x - hw), x1 = (int)std::floor(p.x + hw - 1e-5f);
    int y0 = (int)std::floor(p.y),      y1 = (int)std::floor(p.y + b.height - 1e-5f);
    int z0 = (int)std::floor(p.z - hw), z1 = (int)std::floor(p.z + hw - 1e-5f);
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                if (isCollidable(world.getBlock(x, y, z))) return true;
    return false;
}

void moveBodyAxis(const World& world, Body& b, int axis, float amount) {
    // Step in small increments so fast movement can't tunnel through blocks.
    const float maxStep = 0.45f;
    while (amount != 0.0f) {
        float step = glm::clamp(amount, -maxStep, maxStep);
        amount -= step;
        glm::vec3 next = b.pos;
        next[axis] += step;
        if (!bodyCollidesAt(world, b, next)) {
            b.pos = next;
        } else {
            if (axis == 1) {
                if (b.vel.y < 0.0f) b.onGround = true;
                b.vel.y = 0.0f;
            } else {
                b.vel[axis] = 0.0f;
            }
            break;
        }
    }
}

void moveBody(const World& world, Body& b, float dt) {
    b.onGround = false;
    moveBodyAxis(world, b, 1, b.vel.y * dt); // Y first for ground detection
    moveBodyAxis(world, b, 0, b.vel.x * dt);
    moveBodyAxis(world, b, 2, b.vel.z * dt);
}
```

(`World::getBlock` is const, so `const World&` works throughout.)

- [ ] **Step 5: Refactor `Player` onto `Body`**

`src/Player.h` — remove `void moveAxis(...)` from the private section, add `#include` nothing new (Physics is only needed in the .cpp), and add the interpolation members used by Task 2:

```cpp
    glm::vec3 pos{0.5f, 50.0f, 0.5f}; // feet position
    glm::vec3 prevPos{0.5f, 50.0f, 0.5f}; // position at the previous tick
    ...
    // Fixed-timestep interpolation: call at the start of each simulation
    // tick; render with renderPos/eyePos(alpha), alpha in [0,1).
    void beginTick() { prevPos = pos; }
    glm::vec3 renderPos(float alpha) const { return glm::mix(prevPos, pos, alpha); }
    glm::vec3 eyePos(float alpha) const { return renderPos(alpha) + glm::vec3(0, EYE, 0); }
```

Keep the existing no-arg `eyePos()` (tests/other callers may use it).

`src/Player.cpp` — add `#include "Physics.h"`, delete `Player::moveAxis`, and rewrite:

```cpp
bool Player::collidesAt(World& world, const glm::vec3& p) const {
    Body b;
    b.halfWidth = WIDTH * 0.5f;
    b.height = HEIGHT;
    return bodyCollidesAt(world, b, p);
}
```

and the tail of `Player::update` (replacing `onGround = false; moveAxis(...)×3`):

```cpp
    Body b{pos, vel, WIDTH * 0.5f, HEIGHT, onGround};
    moveBody(world, b, dt);
    pos = b.pos;
    vel = b.vel;
    onGround = b.onGround;
```

In `Player::spawn`, also set `prevPos = pos;` after each assignment (two places), so a respawn doesn't interpolate across the world.

- [ ] **Step 6: CMake**

In `CMakeLists.txt`: add `src/Physics.cpp` to **both** `minecraft` and `world_tests` source lists, and add `src/Player.cpp` to `world_tests`.

- [ ] **Step 7: Build + run tests**

Run: `cmake --build build -j && ./build/world_tests`
Expected: warning-free build, `world_tests` passes (all old + 2 new).

- [ ] **Step 8: Commit**

```bash
git add src/Physics.h src/Physics.cpp src/Player.h src/Player.cpp CMakeLists.txt tests/test_world.cpp
git commit -m "refactor: extract shared sub-stepped AABB physics (Body/moveBody)"
```

---

### Task 2: Fixed-timestep simulation tick (20 TPS) with interpolated rendering

**Files:**
- Modify: `src/main.cpp` (frame loop)

No new headless test (this is frame-loop plumbing); verified by build + `--frames` + the golden screenshot (stationary player ⇒ `prevPos == pos` ⇒ pixel-identical camera).

- [ ] **Step 1: Add the tick constants and accumulator**

In the anonymous namespace of `main.cpp`:

```cpp
constexpr float TICK_DT = 0.05f;        // 20 TPS simulation tick
constexpr int MAX_TICKS_PER_FRAME = 5;  // stall guard: drop time, don't spiral
```

Before the main loop: `double accumulator = 0.0;` and `double gameTime = 0.0;` (drives item bob/spin later — never `glfwGetTime` inside render math, so pausing the sim later stays possible).

- [ ] **Step 2: Move simulation into the accumulator loop**

Replace the direct `app.player.update(world, app.input, dt);` call with:

```cpp
        accumulator += dt;
        gameTime += dt;
        int ticksRun = 0;
        while (accumulator >= TICK_DT) {
            if (++ticksRun > MAX_TICKS_PER_FRAME) { accumulator = 0.0; break; }
            app.player.beginTick();
            app.player.update(world, app.input, TICK_DT);
            // entities tick here from Task 4 on
            accumulator -= TICK_DT;
        }
        const float alpha = float(accumulator / TICK_DT);
```

`world.update(...)` stays per-frame (it's streaming, not simulation). The autosave timer stays per-frame.

- [ ] **Step 3: Interpolate the camera**

Replace `glm::vec3 eye = app.player.eyePos();` with `glm::vec3 eye = app.player.eyePos(alpha);`. The raycast already uses `eye`, so targeting follows the rendered viewpoint automatically. Everything else (frustum, fog) flows from `eye`/`viewProj` unchanged.

- [ ] **Step 4: Build + behavior check**

Run: `cmake --build build -j && ./build/world_tests && ./build/minecraft --frames 300`
Expected: builds clean, tests pass, screenshot renders normally. Run `ctest --test-dir build -R golden` — must pass **unchanged** (stationary player).

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: fixed 20 TPS simulation tick with interpolated rendering"
```

---

### Task 3: Inventory logic (`Inventory.h`, GL-free)

**Files:**
- Create: `src/Inventory.h` (header-only)
- Test: `tests/test_world.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
#include "../src/Inventory.h"

static void testInventory() {
    Inventory inv;
    // Fills hotbar-first, stacks to 64, overflows into the next slot.
    CHECK(inv.add(Block::Dirt, 70) == 0);
    CHECK(inv.slots[0].block == Block::Dirt && inv.slots[0].count == 64);
    CHECK(inv.slots[1].block == Block::Dirt && inv.slots[1].count == 6);
    // Tops up existing stacks before opening a new one.
    CHECK(inv.add(Block::Dirt, 58) == 0);
    CHECK(inv.slots[1].count == 64 && inv.slots[2].empty());
    // Different block goes to the first empty slot.
    CHECK(inv.add(Block::Stone, 1) == 0);
    CHECK(inv.slots[2].block == Block::Stone && inv.slots[2].count == 1);
    // consumeOne decrements and empties.
    CHECK(inv.consumeOne(2));
    CHECK(inv.slots[2].empty());
    CHECK(!inv.consumeOne(2));
    // Full inventory reports leftover.
    Inventory full;
    for (int i = 0; i < Inventory::SLOTS; ++i) CHECK(full.add(Block::Stone, 64) == 0);
    CHECK(full.add(Block::Stone, 10) == 10);
}
```

- [ ] **Step 2: Run to verify it fails** (compile error: no `Inventory.h`).

- [ ] **Step 3: Implement `src/Inventory.h`**

```cpp
#pragma once
#include "Block.h"
#include <algorithm>

struct ItemStack {
    Block block = Block::Air;
    uint8_t count = 0;
    bool empty() const { return count == 0 || block == Block::Air; }
};

// 4 rows x 8 columns of stacks; row 0 (slots 0..7) is the hotbar, matching
// the 8 hotbar keys. Pure logic, GL-free, saved in player.bin v2.
class Inventory {
public:
    static constexpr int COLS = 8, ROWS = 4, SLOTS = COLS * ROWS;
    static constexpr int STACK_MAX = 64;

    ItemStack slots[SLOTS]; // slot 0..7 = hotbar, then the grid rows

    // Add n of b: top up existing stacks first (hotbar first), then fill
    // empty slots. Returns how many didn't fit.
    int add(Block b, int n) {
        if (b == Block::Air || n <= 0) return n < 0 ? 0 : n;
        for (int i = 0; i < SLOTS && n > 0; ++i) {
            ItemStack& s = slots[i];
            if (!s.empty() && s.block == b && s.count < STACK_MAX) {
                int take = std::min(n, STACK_MAX - int(s.count));
                s.count = uint8_t(s.count + take);
                n -= take;
            }
        }
        for (int i = 0; i < SLOTS && n > 0; ++i) {
            ItemStack& s = slots[i];
            if (s.empty()) {
                int take = std::min(n, STACK_MAX);
                s = {b, uint8_t(take)};
                n -= take;
            }
        }
        return n;
    }

    // Remove one item (a placement). False if the slot is empty/invalid.
    bool consumeOne(int slot) {
        if (slot < 0 || slot >= SLOTS) return false;
        ItemStack& s = slots[slot];
        if (s.empty()) return false;
        if (--s.count == 0) s.block = Block::Air;
        return true;
    }
};
```

- [ ] **Step 4: Build + run tests** — `cmake --build build -j && ./build/world_tests` ⇒ PASS.

- [ ] **Step 5: Commit**

```bash
git add src/Inventory.h tests/test_world.cpp
git commit -m "feat: inventory stack logic (4x8 slots, hotbar row 0)"
```

---

### Task 4: Entity system — `ItemEntity` + `Entities` manager with per-chunk buckets

**Files:**
- Create: `src/Entity.h`, `src/Entity.cpp`
- Modify: `CMakeLists.txt` (add `src/Entity.cpp` to both targets)
- Test: `tests/test_world.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
#include "../src/Entity.h"

static void testItemEntityFallsAndLands() {
    std::filesystem::remove_all("test_ent_save");
    World w(1337, "test_ent_save");
    w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 1, 10000);
    w.setBlock(0, 70, 0, Block::Stone);
    Entities ents;
    ents.spawnItem(glm::vec3(0.5f, 75.0f, 0.5f), glm::vec3(0.0f), Block::Dirt, 1);
    glm::vec3 farAway(500.0f, 50.0f, 500.0f); // out of magnet range
    for (int i = 0; i < 100; ++i) ents.tick(w, farAway, nullptr, 0.05f);
    CHECK(ents.items().size() == 1);
    const ItemEntity& e = *ents.items()[0];
    CHECK(e.body.onGround);
    CHECK(std::abs(e.body.pos.y - 71.0f) < 1e-3f);
    std::filesystem::remove_all("test_ent_save");
}

static void testItemPickup() {
    std::filesystem::remove_all("test_ent_save2");
    World w(1337, "test_ent_save2");
    w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 1, 10000);
    w.setBlock(0, 70, 0, Block::Stone);
    Entities ents;
    Inventory inv;
    ents.spawnItem(glm::vec3(0.5f, 71.0f, 0.5f), glm::vec3(0.0f), Block::Dirt, 1);
    glm::vec3 playerPos(1.5f, 71.0f, 0.5f); // one block away: in magnet range
    for (int i = 0; i < 60 && !ents.items().empty(); ++i)
        ents.tick(w, playerPos, &inv, 0.05f);
    CHECK(ents.items().empty()); // magnetized in and collected
    CHECK(inv.slots[0].block == Block::Dirt && inv.slots[0].count == 1);
    std::filesystem::remove_all("test_ent_save2");
}

static void testItemFrozenInUnloadedChunk() {
    std::filesystem::remove_all("test_ent_save3");
    World w(1337, "test_ent_save3");
    w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 1, 10000);
    Entities ents;
    glm::vec3 farPos(1000.5f, 50.0f, 1000.5f); // chunk never loaded
    ents.spawnItem(farPos, glm::vec3(0.0f), Block::Stone, 1);
    for (int i = 0; i < 20; ++i) ents.tick(w, glm::vec3(0.0f), nullptr, 0.05f);
    CHECK(ents.items()[0]->body.pos == farPos); // no physics in the void
    std::filesystem::remove_all("test_ent_save3");
}

static void testEntityBucketsAndDrops() {
    std::filesystem::remove_all("test_ent_save4");
    World w(1337, "test_ent_save4");
    w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 2, 10000);
    Entities ents;
    // Registry-driven drops: Grass drops Dirt, Leaves drop nothing.
    ents.spawnBlockDrop(glm::ivec3(0, 70, 0), Block::Grass);
    ents.spawnBlockDrop(glm::ivec3(0, 70, 0), Block::Leaves);
    CHECK(ents.items().size() == 1);
    CHECK(ents.items()[0]->item == Block::Dirt);
    // Bucket query: a second item two chunks away is not "near".
    ents.spawnItem(glm::vec3(40.5f, 70.0f, 0.5f), glm::vec3(0.0f), Block::Stone, 1);
    ents.tick(w, glm::vec3(500.0f, 50.0f, 500.0f), nullptr, 0.05f);
    CHECK(ents.itemsNear(glm::vec3(0.5f, 70.0f, 0.5f), 8.0f).size() == 1);
    CHECK(ents.itemsNear(glm::vec3(40.5f, 70.0f, 0.5f), 8.0f).size() == 1);
    std::filesystem::remove_all("test_ent_save4");
}
```

(Note `testEntityBucketsAndDrops` needs the chunk at x=40 loaded — radius 2 around origin covers cx 0..2, i.e. x up to 47. ✓)

- [ ] **Step 2: Run to verify compile failure.**

- [ ] **Step 3: Implement `src/Entity.h`**

```cpp
#pragma once
#include "Block.h"
#include "Inventory.h"
#include "Physics.h"
#include "World.h" // ChunkKey/ChunkKeyHash; World is GL-free until upload
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

// The first entity: a dropped item. A small cube body that falls with the
// shared sub-stepped AABB collision, magnetizes to the player, and is
// collected into the inventory. Bob/spin are render-only (ItemRenderer).
struct ItemEntity {
    Body body;             // halfWidth 0.125, height 0.25
    glm::vec3 prevPos{0};  // previous-tick position for render interpolation
    Block item = Block::Air;
    int count = 1;
    float age = 0.0f;      // seconds since spawn (pickup delay, despawn)
    uint32_t spinSeed = 0; // de-syncs bob/spin phase between items
    bool dead = false;
    glm::vec3 renderPos(float alpha) const { return glm::mix(prevPos, body.pos, alpha); }
};

// Entity manager. Main thread only, like the chunk map: workers never see
// entities. Items in unloaded chunks freeze (no physics, no aging) — the
// world there is undefined and they would fall forever. Entities are not
// persisted across runs (documented Batch G limitation).
class Entities {
public:
    void spawnItem(const glm::vec3& pos, const glm::vec3& vel, Block item, int count = 1);
    // Drop for a broken block: the registry's `drop`, tossed from the block
    // center with a small pseudo-random horizontal kick. No-op for Air drops.
    void spawnBlockDrop(const glm::ivec3& blockPos, Block broken);

    // One simulation tick: physics, magnetized pickup into `inv` (skipped
    // when inv is null), despawn, then rebuild the per-chunk buckets.
    void tick(const World& world, const glm::vec3& playerPos, Inventory* inv, float dt);

    const std::vector<std::unique_ptr<ItemEntity>>& items() const { return items_; }
    // Bucket-accelerated proximity query (used by tests now, mobs later).
    std::vector<ItemEntity*> itemsNear(const glm::vec3& pos, float radius) const;

private:
    std::vector<std::unique_ptr<ItemEntity>> items_;
    std::unordered_map<ChunkKey, std::vector<ItemEntity*>, ChunkKeyHash> buckets_;
    uint32_t rng_ = 0x9E3779B9u;
    float rand01();
};
```

- [ ] **Step 4: Implement `src/Entity.cpp`**

```cpp
#include "Entity.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float ITEM_GRAVITY = -22.0f;
constexpr float ITEM_TERMINAL = -30.0f;
constexpr float GROUND_FRICTION = 0.6f; // horizontal damping per tick on ground
constexpr float MAGNET_RADIUS = 2.0f;   // starts flying to the player inside this
constexpr float MAGNET_SPEED = 8.0f;
constexpr float PICKUP_RADIUS = 0.8f;
constexpr float PICKUP_DELAY = 0.4f;    // so fresh drops visibly pop out first
constexpr float DESPAWN_SECONDS = 300.0f;
}

float Entities::rand01() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return float(rng_ >> 8) / float(1u << 24);
}

void Entities::spawnItem(const glm::vec3& pos, const glm::vec3& vel, Block item, int count) {
    auto e = std::make_unique<ItemEntity>();
    e->body.pos = pos;
    e->body.vel = vel;
    e->body.halfWidth = 0.125f;
    e->body.height = 0.25f;
    e->prevPos = pos;
    e->item = item;
    e->count = count;
    e->spinSeed = rng_;
    items_.push_back(std::move(e));
}

void Entities::spawnBlockDrop(const glm::ivec3& blockPos, Block broken) {
    Block drop = blockDef(broken).drop;
    if (drop == Block::Air) return;
    float a = rand01() * 6.2831853f;
    spawnItem(glm::vec3(blockPos) + glm::vec3(0.5f, 0.4f, 0.5f),
              glm::vec3(std::cos(a) * 1.5f, 3.5f, std::sin(a) * 1.5f), drop, 1);
}

void Entities::tick(const World& world, const glm::vec3& playerPos, Inventory* inv, float dt) {
    const glm::vec3 target = playerPos + glm::vec3(0, 0.9f, 0); // player torso
    for (auto& up : items_) {
        ItemEntity& e = *up;
        e.prevPos = e.body.pos;
        if (!world.isAreaReady(e.body.pos, 0)) continue; // frozen in the void
        e.age += dt;
        glm::vec3 center = e.body.pos + glm::vec3(0, e.body.height * 0.5f, 0);
        bool canPick = e.age >= PICKUP_DELAY && inv != nullptr;
        if (canPick && glm::distance(center, target) < MAGNET_RADIUS) {
            e.body.vel = glm::normalize(target - center) * MAGNET_SPEED;
        } else {
            e.body.vel.y += ITEM_GRAVITY * dt;
            if (e.body.vel.y < ITEM_TERMINAL) e.body.vel.y = ITEM_TERMINAL;
            if (e.body.onGround) {
                e.body.vel.x *= GROUND_FRICTION;
                e.body.vel.z *= GROUND_FRICTION;
            }
        }
        moveBody(world, e.body, dt);
        center = e.body.pos + glm::vec3(0, e.body.height * 0.5f, 0);
        if (canPick && glm::distance(center, target) < PICKUP_RADIUS) {
            int leftover = inv->add(e.item, e.count);
            if (leftover == 0) e.dead = true;
            else e.count = leftover; // inventory full: keep the remainder
        }
        if (e.age > DESPAWN_SECONDS) e.dead = true;
    }
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                     [](const std::unique_ptr<ItemEntity>& e) { return e->dead; }),
                 items_.end());
    buckets_.clear();
    for (auto& up : items_) {
        ChunkKey k{World::floorDiv((int)std::floor(up->body.pos.x), CHUNK_SIZE),
                   World::floorDiv((int)std::floor(up->body.pos.z), CHUNK_SIZE)};
        buckets_[k].push_back(up.get());
    }
}

std::vector<ItemEntity*> Entities::itemsNear(const glm::vec3& pos, float radius) const {
    std::vector<ItemEntity*> out;
    int cx0 = World::floorDiv((int)std::floor(pos.x - radius), CHUNK_SIZE);
    int cx1 = World::floorDiv((int)std::floor(pos.x + radius), CHUNK_SIZE);
    int cz0 = World::floorDiv((int)std::floor(pos.z - radius), CHUNK_SIZE);
    int cz1 = World::floorDiv((int)std::floor(pos.z + radius), CHUNK_SIZE);
    for (int cz = cz0; cz <= cz1; ++cz)
        for (int cx = cx0; cx <= cx1; ++cx) {
            auto it = buckets_.find(ChunkKey{cx, cz});
            if (it == buckets_.end()) continue;
            for (ItemEntity* e : it->second)
                if (glm::distance(e->body.pos, pos) <= radius) out.push_back(e);
        }
    return out;
}
```

`World::isAreaReady(pos, 0)` must mean "the chunk containing pos is loaded" — **verify this in World.cpp while implementing**; if its radius semantics differ, add a tiny `bool isChunkLoaded(int cx, int cz) const` to World instead. `moveBody` takes `const World&` (Task 1), so `tick` can too.

- [ ] **Step 5: CMake** — add `src/Entity.cpp` to both `minecraft` and `world_tests`.

- [ ] **Step 6: Build + run tests** — all pass, warning-free.

- [ ] **Step 7: Commit**

```bash
git add src/Entity.h src/Entity.cpp CMakeLists.txt tests/test_world.cpp
git commit -m "feat: entity foundation — dropped items with magnetized pickup"
```

---

### Task 5: Survival mode flag, block drops, finite placement, hotbar counts

**Files:**
- Modify: `src/Settings.h` (add `survival`)
- Modify: `src/main.cpp`

Logic was tested in Tasks 3–4; this task is wiring. Verified by build + tests + a manual `--frames` run.

- [ ] **Step 1: Settings flag**

In `Settings`: add `bool survival = false;`, parse `else if (key == "survival") s.survival = std::stoi(val) != 0;`, and write `f << "survival=" << (survival ? 1 : 0) << "\n";` in `save`.

- [ ] **Step 2: App state + held block**

In `main.cpp` add includes `#include "Entity.h"` and `#include "Inventory.h"`. Extend `App`:

```cpp
    Inventory inv;
    ItemStack cursorStack;   // stack held by the mouse in the inventory UI (Task 7)
    Entities entities;
    bool survival = false;
    bool invOpen = false;
```

After `Settings settings = Settings::load(...)` in `main()`: `app.survival = settings.survival;`.

Rewrite `heldBlock()`:

```cpp
Block heldBlock() {
    if (!app.survival) return HOTBAR[app.hotbarSlot];
    const ItemStack& s = app.inv.slots[app.hotbarSlot];
    return s.empty() ? Block::Air : s.block;
}
```

- [ ] **Step 3: Tick entities**

In the Task 2 accumulator loop, after `app.player.update(...)`:

```cpp
            app.entities.tick(world, app.player.pos, &app.inv, TICK_DT);
```

- [ ] **Step 4: Drops on break, consumption on place**

Replace the break/place block:

```cpp
        if (app.breakPressed && hit.hit &&
            isBreakable(world.getBlock(hit.block.x, hit.block.y, hit.block.z))) {
            Block broken = world.getBlock(hit.block.x, hit.block.y, hit.block.z);
            world.setBlock(hit.block.x, hit.block.y, hit.block.z, Block::Air);
            if (app.survival) app.entities.spawnBlockDrop(hit.block, broken);
        }
        if (app.placePressed && hit.hit) {
            glm::ivec3 p = hit.adjacent;
            Block held = heldBlock();
            if (held != Block::Air &&
                !isSolid(world.getBlock(p.x, p.y, p.z)) && !app.player.intersectsBlock(p)) {
                if (!app.survival || app.inv.consumeOne(app.hotbarSlot))
                    world.setBlock(p.x, p.y, p.z, held);
            }
        }
```

- [ ] **Step 5: Hotbar counts in survival**

In `drawHotbar`, replace the icon block with a mode branch:

```cpp
        if (app.survival) {
            const ItemStack& s = app.inv.slots[i];
            if (!s.empty()) {
                hud.drawTile(x + pad, y + pad, icon, tileFor(s.block, 4), sel ? 1.0f : 0.8f);
                if (s.count > 1) {
                    char cnt[4];
                    std::snprintf(cnt, sizeof(cnt), "%d", s.count);
                    float cw = std::strlen(cnt) * Hud::GLYPH * 1.5f;
                    hud.drawText(x + slot - cw - 4, y + slot - 16, 1.5f, cnt);
                }
            }
        } else {
            hud.drawTile(x + pad, y + pad, icon, tileFor(HOTBAR[i], 4), sel ? 1.0f : 0.8f);
        }
```

And the name label under the hotbar: `const char* name = blockName(heldBlock());` already does the right thing once `heldBlock()` is mode-aware (`Air` shows as "Air" for an empty slot — change to show `""` by skipping the label when `heldBlock() == Block::Air && app.survival`).

- [ ] **Step 6: Build + tests + sanity run**

`cmake --build build -j && ./build/world_tests && ./build/minecraft --frames 300` (creative default — image must look unchanged; golden test still passes).

- [ ] **Step 7: Commit**

```bash
git add src/Settings.h src/main.cpp
git commit -m "feat: survival mode — block drops, finite placement, hotbar counts"
```

---

### Task 6: Item entity rendering (bobbing, spinning textured cubes)

**Files:**
- Create: `src/ItemRenderer.h`, `src/ItemRenderer.cpp`
- Modify: `src/main.cpp`, `CMakeLists.txt` (`minecraft` target only — GL code)

- [ ] **Step 1: Implement `src/ItemRenderer.h`**

```cpp
#pragma once
#include <glm/glm.hpp>

class World;
class Entities;

// Draws item entities as small spinning, bobbing textured cubes, lit by the
// world light at their cell. Uses the block texture array, which the caller
// leaves bound on unit 0 (same as chunk drawing). GL half only — all item
// logic lives in Entity.cpp.
class ItemRenderer {
public:
    ItemRenderer();
    ~ItemRenderer();
    ItemRenderer(const ItemRenderer&) = delete;
    ItemRenderer& operator=(const ItemRenderer&) = delete;

    void draw(const World& world, const Entities& entities,
              const glm::mat4& viewProj, float alpha, float time);

private:
    unsigned vao_ = 0, vbo_ = 0, prog_ = 0;
    int locMVP_ = -1, locLayers_ = -1, locLight_ = -1;
};
```

- [ ] **Step 2: Implement `src/ItemRenderer.cpp`**

```cpp
#include "ItemRenderer.h"
#include "Block.h"
#include "Entity.h"
#include "Shader.h"
#include "World.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <cmath>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {
const char* ITEM_VS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in float aFace;
layout(location = 3) in float aShade;
uniform mat4 uMVP;
uniform float uLight;
out vec2 vUV;
flat out int vFace;
out float vLight;
void main() {
    vUV = aUV;
    vFace = int(aFace + 0.5);
    vLight = aShade * uLight;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* ITEM_FS = R"(
#version 330 core
in vec2 vUV;
flat in int vFace;
in float vLight;
uniform sampler2DArray uAtlas;
uniform float uLayers[6];
out vec4 FragColor;
void main() {
    vec3 c = texture(uAtlas, vec3(vUV, uLayers[vFace])).rgb * vLight;
    FragColor = vec4(c, 1.0);
}
)";

// Unit cube, x/z in [-0.5,0.5], y in [0,1] (feet origin like Body.pos).
// Face order matches Block.h tiles: +X -X +Y -Y +Z -Z. Drawn with face
// culling disabled (a handful of cubes), so winding doesn't matter.
std::vector<float> buildCube() {
    std::vector<float> v;
    auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                    int face, float shade) {
        const glm::vec3 P[6] = {a, b, c, a, c, d};
        const float U[6] = {0, 1, 1, 0, 1, 0};
        const float W[6] = {1, 1, 0, 1, 0, 0};
        for (int i = 0; i < 6; ++i)
            v.insert(v.end(), {P[i].x, P[i].y, P[i].z, U[i], W[i],
                               float(face), shade});
    };
    const float l = -0.5f, r = 0.5f, b = 0.0f, t = 1.0f;
    quad({r, b, l}, {r, b, r}, {r, t, r}, {r, t, l}, 0, 0.80f); // +X
    quad({l, b, r}, {l, b, l}, {l, t, l}, {l, t, r}, 1, 0.80f); // -X
    quad({l, t, l}, {r, t, l}, {r, t, r}, {l, t, r}, 2, 1.00f); // +Y
    quad({l, b, r}, {r, b, r}, {r, b, l}, {l, b, l}, 3, 0.60f); // -Y
    quad({r, b, r}, {l, b, r}, {l, t, r}, {r, t, r}, 4, 0.70f); // +Z
    quad({l, b, l}, {r, b, l}, {r, t, l}, {l, t, l}, 5, 0.70f); // -Z
    return v;
}
} // namespace

ItemRenderer::ItemRenderer() {
    prog_ = compileProgram(ITEM_VS, ITEM_FS); // see step note below
    locMVP_ = glGetUniformLocation(prog_, "uMVP");
    locLayers_ = glGetUniformLocation(prog_, "uLayers");
    locLight_ = glGetUniformLocation(prog_, "uLight");
    glUseProgram(prog_);
    glUniform1i(glGetUniformLocation(prog_, "uAtlas"), 0);

    std::vector<float> verts = buildCube();
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(),
                 GL_STATIC_DRAW);
    const GLsizei stride = 7 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    for (int i = 0; i < 4; ++i) glEnableVertexAttribArray(i);
    glBindVertexArray(0);
}

ItemRenderer::~ItemRenderer() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(prog_);
}

void ItemRenderer::draw(const World& world, const Entities& entities,
                        const glm::mat4& viewProj, float alpha, float time) {
    if (entities.items().empty()) return;
    glUseProgram(prog_);
    glDisable(GL_CULL_FACE);
    for (const auto& up : entities.items()) {
        const ItemEntity& e = *up;
        glm::vec3 p = e.renderPos(alpha);
        float phase = float(e.spinSeed % 628u) * 0.01f;
        float bob = 0.06f + 0.05f * std::sin(time * 2.0f + phase);
        glm::mat4 m = glm::translate(glm::mat4(1.0f), p + glm::vec3(0, bob, 0));
        m = glm::rotate(m, time * 1.5f + phase, glm::vec3(0, 1, 0));
        m = glm::scale(m, glm::vec3(0.25f));
        glm::mat4 mvp = viewProj * m;
        glUniformMatrix4fv(locMVP_, 1, GL_FALSE, glm::value_ptr(mvp));
        float layers[6];
        for (int f = 0; f < 6; ++f) layers[f] = float(tileFor(e.item, f));
        glUniform1fv(locLayers_, 6, layers);
        int wx = (int)std::floor(p.x), wy = (int)std::floor(p.y + 0.2f),
            wz = (int)std::floor(p.z);
        int level = std::max(world.sunLightAt(wx, wy, wz),
                             world.blockLightAt(wx, wy, wz));
        glUniform1f(locLight_, std::pow(0.85f, float(15 - level)));
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    glEnable(GL_CULL_FACE);
}
```

**Shader compile note:** check `src/Shader.h` first — if the `Shader` class is usable here (it takes VS/FS source strings), hold a `Shader` member instead of a raw `prog_` and use its `loc`/`use`/`setMat4` helpers. Match whatever `Shader.h` actually offers; don't write a duplicate `compileProgram` if one exists.

- [ ] **Step 3: Wire into `main.cpp`**

Add `#include "ItemRenderer.h"`. After `Hud hud(atlas);`: `ItemRenderer itemRenderer;`. Between the opaque pass and the water pass:

```cpp
        world.drawChunks(frustum, eye, originLoc);

        // Item entities: after opaque (depth-tested normally), before water
        // (so submerged drops blend correctly under the surface).
        itemRenderer.draw(world, app.entities, viewProj, alpha, float(gameTime));

        // Translucent water pass ... (re-bind the chunk shader first!)
        chunkShader.use();
        chunkShader.setFloat("uAlpha", 0.65f);
```

(The existing code assumed `chunkShader` stayed bound after `drawChunks`; the `chunkShader.use()` line is the required fix.)

- [ ] **Step 4: Demo flag for screenshot verification**

Breaking blocks can't be scripted in a `--frames` run, so add a debug flag (alongside `--frames`/`--bench` parsing):

```cpp
    bool demoItems = false;
    ...
    if (std::strcmp(argv[i], "--demo-items") == 0) demoItems = true; // (in the argv loop; this one takes no value, so also scan argv[argc-1])
```

Parse it by scanning **all** of argv (the existing loop stops at `argc - 1`; flag-only args need `for (int i = 1; i < argc; ++i)` — restructure the loop to check value-taking flags only when `i < argc - 1`). After `world.waitUntilLoaded(...)` and player setup:

```cpp
    if (demoItems) {
        glm::vec3 e = app.player.eyePos();
        glm::vec3 d = app.player.lookDir();
        glm::vec3 base = e + d * 3.0f;
        app.entities.spawnItem(base, glm::vec3(0), Block::Dirt, 1);
        app.entities.spawnItem(base + glm::vec3(1, 0, 0), glm::vec3(0), Block::Stone, 1);
        app.entities.spawnItem(base + glm::vec3(-1, 0, 0), glm::vec3(0), Block::Wood, 1);
    }
```

- [ ] **Step 5: CMake** — add `src/ItemRenderer.cpp` to the `minecraft` target only.

- [ ] **Step 6: Build + visual verification**

```sh
cmake --build build -j && ./build/world_tests
./build/minecraft --demo-items --frames 300
# convert screenshot.ppm -> png and LOOK at it: three small textured cubes
# floating/bobbing in front of the spawn viewpoint, lit plausibly.
```

Also run the plain golden test (`ctest --test-dir build -R golden`) — no items spawn without the flag, image unchanged.

- [ ] **Step 7: Commit**

```bash
git add src/ItemRenderer.h src/ItemRenderer.cpp src/main.cpp CMakeLists.txt
git commit -m "feat: render item entities as bobbing, spinning textured cubes"
```

---### Task 7: Inventory grid UI (E to open, click to move stacks)

**Files:**
- Modify: `src/main.cpp` only (uses existing `Hud` primitives)

- [ ] **Step 1: Layout + hit-testing helpers** (anonymous namespace, near `drawHotbar`)

```cpp
// Inventory grid layout: rows 1..3 (main grid) on top, row 0 (the hotbar
// row) below with a gap — mirrors the on-screen hotbar.
struct InvLayout { float slot = 56.0f, pad = 4.0f; float x0 = 0, y0 = 0; };

InvLayout invLayout(int w, int h) {
    InvLayout L;
    float gw = Inventory::COLS * L.slot + (Inventory::COLS - 1) * L.pad;
    float gh = Inventory::ROWS * L.slot + (Inventory::ROWS - 1) * L.pad + 14.0f;
    L.x0 = (w - gw) * 0.5f;
    L.y0 = (h - gh) * 0.5f;
    return L;
}

float invSlotY(const InvLayout& L, int row) { // row 0 = hotbar, drawn last
    if (row == 0) return L.y0 + 3 * (L.slot + L.pad) + 14.0f;
    return L.y0 + (row - 1) * (L.slot + L.pad);
}

int invSlotAt(int w, int h, float mx, float my) {
    InvLayout L = invLayout(w, h);
    for (int i = 0; i < Inventory::SLOTS; ++i) {
        float x = L.x0 + (i % Inventory::COLS) * (L.slot + L.pad);
        float y = invSlotY(L, i / Inventory::COLS);
        if (mx >= x && mx < x + L.slot && my >= y && my < y + L.slot) return i;
    }
    return -1;
}
```

- [ ] **Step 2: Stack-move click logic + open/close**

```cpp
void invClick(int slot) {
    if (slot < 0) return;
    ItemStack& s = app.inv.slots[slot];
    ItemStack& c = app.cursorStack;
    if (c.empty()) {            // pick up
        c = s; s = {};
    } else if (s.empty() || s.block != c.block) { // place into empty / swap
        std::swap(s, c);
    } else {                    // merge same-block stacks
        int space = Inventory::STACK_MAX - int(s.count);
        int moved = std::min(space, int(c.count));
        s.count = uint8_t(s.count + moved);
        c.count = uint8_t(c.count - moved);
        if (c.count == 0) c = {};
    }
}

void closeInventory(GLFWwindow* w) {
    app.invOpen = false;
    if (!app.cursorStack.empty()) { // never destroy items on close
        int leftover = app.inv.add(app.cursorStack.block, app.cursorStack.count);
        if (leftover > 0)
            app.entities.spawnItem(app.player.eyePos(), app.player.lookDir() * 3.0f,
                                   app.cursorStack.block, leftover);
        app.cursorStack = {};
    }
    app.mouseCaptured = true;
    app.firstMouse = true;
    glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}
```

(`std::swap` needs `<utility>` — `<vector>` usually pulls it in, include explicitly.)

- [ ] **Step 3: Input routing**

`keyCallback` — add before the existing switch handling:

```cpp
            case GLFW_KEY_E:
                if (!app.survival) break;
                if (app.invOpen) {
                    closeInventory(w);
                } else {
                    app.invOpen = true;
                    app.mouseCaptured = false;
                    glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
                break;
```

and make `GLFW_KEY_ESCAPE` close the inventory first:

```cpp
            case GLFW_KEY_ESCAPE:
                if (app.invOpen) { closeInventory(w); break; }
                ... // existing capture/quit logic
```

`mouseButtonCallback` — route clicks while the inventory is open (before the recapture branch):

```cpp
    if (app.invOpen) {
        if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
            double mx, my;
            glfwGetCursorPos(w, &mx, &my);
            int ww, wh;
            glfwGetWindowSize(w, &ww, &wh); // cursor coords are window coords
            invClick(invSlotAt(ww, wh, float(mx), float(my)));
        }
        return;
    }
```

While `app.invOpen`, freeze player *intent* but keep simulating (gravity etc.): in the tick loop pass an empty input — `app.player.update(world, app.invOpen ? PlayerInput{} : app.input, TICK_DT);`.

- [ ] **Step 4: Drawing**

```cpp
void drawInventory(Hud& hud, GLFWwindow* w, int screenW, int screenH) {
    hud.drawRect(0, 0, float(screenW), float(screenH), 0, 0, 0, 0.55f);
    InvLayout L = invLayout(screenW, screenH);
    hud.drawText(L.x0, L.y0 - 26.0f, 2.0f, "Inventory");
    for (int i = 0; i < Inventory::SLOTS; ++i) {
        float x = L.x0 + (i % Inventory::COLS) * (L.slot + L.pad);
        float y = invSlotY(L, i / Inventory::COLS);
        hud.drawRect(x, y, L.slot, L.slot, 0.15f, 0.15f, 0.15f, 0.9f);
        const ItemStack& s = app.inv.slots[i];
        if (s.empty()) continue;
        hud.drawTile(x + L.pad, y + L.pad, L.slot - 2 * L.pad, tileFor(s.block, 4));
        if (s.count > 1) {
            char cnt[4];
            std::snprintf(cnt, sizeof(cnt), "%d", s.count);
            float cw = std::strlen(cnt) * Hud::GLYPH * 1.5f;
            hud.drawText(x + L.slot - cw - 4, y + L.slot - 16, 1.5f, cnt);
        }
    }
    if (!app.cursorStack.empty()) { // stack riding the mouse
        double mx, my;
        glfwGetCursorPos(w, &mx, &my);
        hud.drawTile(float(mx) - 20, float(my) - 20, 40, tileFor(app.cursorStack.block, 4));
        if (app.cursorStack.count > 1) {
            char cnt[4];
            std::snprintf(cnt, sizeof(cnt), "%d", app.cursorStack.count);
            hud.drawText(float(mx) + 6, float(my) + 6, 1.5f, cnt);
        }
    }
}
```

In the HUD section of the frame loop, after `drawHotbar(...)`:

```cpp
        if (app.invOpen) drawInventory(hud, app.window, width, height);
```

(Skip `drawCrosshair` while open: `if (!app.invOpen) drawCrosshair(...)`.)

- [ ] **Step 5: Demo flag for the screenshot**

Add `--demo-inv`: sets `app.survival = true`, stocks a few stacks, opens the inventory after startup:

```cpp
    if (demoInv) {
        app.survival = true;
        app.inv.add(Block::Dirt, 80);
        app.inv.add(Block::Stone, 64);
        app.inv.add(Block::Wood, 5);
        app.inv.add(Block::Torch, 3);
        app.invOpen = true;
        app.mouseCaptured = false;
        glfwSetInputMode(app.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
```

- [ ] **Step 6: Build + visual verification**

```sh
cmake --build build -j && ./build/world_tests
./build/minecraft --demo-inv --frames 120
# convert + LOOK: dimmed world, 3 grid rows + separated hotbar row, stacks
# with counts (64/16 dirt split across two slots, 64 stone, 5 wood, 3 torch).
```

Interactive check on the real display (run briefly, no flags, `survival=1` in settings.cfg): E opens/closes, clicking moves/merges/swaps stacks, Escape closes, counts update, held stack follows the cursor.

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp
git commit -m "feat: inventory grid UI — E to open, click to move stacks"
```

---

### Task 8: Player save v2 (`PlayerSave.h`) with inventory + v1 migration

**Files:**
- Create: `src/PlayerSave.h` (header-only, GL-free)
- Modify: `src/main.cpp` (savePlayer/loadPlayer delegate; remove the old constants)
- Test: `tests/test_world.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
#include "../src/PlayerSave.h"

static void testPlayerSaveV2Roundtrip() {
    std::filesystem::create_directories("test_psave");
    PlayerState a;
    a.pos = glm::vec3(12.5f, 34.0f, -8.25f);
    a.yaw = 123.0f; a.pitch = -45.0f;
    a.flying = true; a.hotbarSlot = 3;
    a.inv.add(Block::Dirt, 70);
    a.inv.add(Block::Torch, 5);
    CHECK(savePlayerFile("test_psave/player.bin", a));
    PlayerState b;
    CHECK(loadPlayerFile("test_psave/player.bin", b));
    CHECK(b.pos == a.pos && b.yaw == a.yaw && b.pitch == a.pitch);
    CHECK(b.flying && b.hotbarSlot == 3);
    for (int i = 0; i < Inventory::SLOTS; ++i) {
        CHECK(b.inv.slots[i].block == a.inv.slots[i].block);
        CHECK(b.inv.slots[i].count == a.inv.slots[i].count);
    }
    std::filesystem::remove_all("test_psave");
}

static void testPlayerSaveV1Migrates() {
    // Hand-craft a v1 file: header + pos + yaw + pitch + flying + slot.
    std::filesystem::create_directories("test_psave1");
    {
        std::ofstream f("test_psave1/player.bin", std::ios::binary);
        f.write("MCPL", 4);
        uint32_t v = 1;
        f.write(reinterpret_cast<const char*>(&v), 4);
        glm::vec3 pos(1.0f, 2.0f, 3.0f);
        float yaw = 10.0f, pitch = 20.0f;
        f.write(reinterpret_cast<const char*>(&pos), sizeof(pos));
        f.write(reinterpret_cast<const char*>(&yaw), 4);
        f.write(reinterpret_cast<const char*>(&pitch), 4);
        uint8_t flying = 1, slot = 2;
        f.write(reinterpret_cast<const char*>(&flying), 1);
        f.write(reinterpret_cast<const char*>(&slot), 1);
    }
    PlayerState s;
    CHECK(loadPlayerFile("test_psave1/player.bin", s));
    CHECK(s.pos == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(s.flying && s.hotbarSlot == 2);
    for (int i = 0; i < Inventory::SLOTS; ++i) CHECK(s.inv.slots[i].empty());
    // Bad magic is rejected.
    {
        std::ofstream f("test_psave1/bad.bin", std::ios::binary);
        f.write("XXXX", 4);
    }
    CHECK(!loadPlayerFile("test_psave1/bad.bin", s));
    std::filesystem::remove_all("test_psave1");
}
```

- [ ] **Step 2: Run to verify compile failure.**

- [ ] **Step 3: Implement `src/PlayerSave.h`**

```cpp
#pragma once
#include "Inventory.h"
#include "SaveIO.h"
#include <cstring>
#include <fstream>
#include <glm/glm.hpp>
#include <string>

// Player persistence, v2: v1 fields + 32 inventory slots (block byte, count
// byte). v1 files still load (empty inventory) so existing saves keep their
// position — the header is only rejected when magic/version is unknown.
struct PlayerState {
    glm::vec3 pos{0.5f, 50.0f, 0.5f};
    float yaw = -90.0f, pitch = 0.0f;
    bool flying = false;
    uint8_t hotbarSlot = 0;
    Inventory inv;
};

constexpr char PLAYER_MAGIC[4] = {'M', 'C', 'P', 'L'};
constexpr uint32_t PLAYER_VERSION = 2;

inline bool savePlayerFile(const std::string& path, const PlayerState& s) {
    return atomicSave(path, [&](std::ofstream& f) {
        f.write(PLAYER_MAGIC, 4);
        f.write(reinterpret_cast<const char*>(&PLAYER_VERSION), 4);
        f.write(reinterpret_cast<const char*>(&s.pos), sizeof(s.pos));
        f.write(reinterpret_cast<const char*>(&s.yaw), sizeof(s.yaw));
        f.write(reinterpret_cast<const char*>(&s.pitch), sizeof(s.pitch));
        uint8_t flying = s.flying ? 1 : 0;
        f.write(reinterpret_cast<const char*>(&flying), 1);
        f.write(reinterpret_cast<const char*>(&s.hotbarSlot), 1);
        for (int i = 0; i < Inventory::SLOTS; ++i) {
            uint8_t b = uint8_t(s.inv.slots[i].block);
            uint8_t c = s.inv.slots[i].count;
            f.write(reinterpret_cast<const char*>(&b), 1);
            f.write(reinterpret_cast<const char*>(&c), 1);
        }
    });
}

inline bool loadPlayerFile(const std::string& path, PlayerState& s) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4];
    uint32_t version = 0;
    f.read(magic, 4);
    f.read(reinterpret_cast<char*>(&version), 4);
    if (!f || std::memcmp(magic, PLAYER_MAGIC, 4) != 0 ||
        (version != 1 && version != 2))
        return false;
    f.read(reinterpret_cast<char*>(&s.pos), sizeof(s.pos));
    f.read(reinterpret_cast<char*>(&s.yaw), sizeof(s.yaw));
    f.read(reinterpret_cast<char*>(&s.pitch), sizeof(s.pitch));
    uint8_t flying = 0, slot = 0;
    f.read(reinterpret_cast<char*>(&flying), 1);
    f.read(reinterpret_cast<char*>(&slot), 1);
    if (!f) return false;
    s.flying = flying != 0;
    s.hotbarSlot = slot;
    s.inv = Inventory{};
    if (version >= 2) {
        for (int i = 0; i < Inventory::SLOTS; ++i) {
            uint8_t b = 0, c = 0;
            f.read(reinterpret_cast<char*>(&b), 1);
            f.read(reinterpret_cast<char*>(&c), 1);
            if (!f) return false;
            if (b >= BLOCK_TYPES) { b = 0; c = 0; } // clamp unknown ids, like chunk load
            s.inv.slots[i] = {Block(b), b == 0 ? uint8_t(0) : c};
        }
    }
    return true;
}
```

**Check `src/SaveIO.h` for `atomicSave`'s exact callable signature before writing this** (main.cpp uses `atomicSave(path, [](std::ofstream& f) {...})` — match it).

- [ ] **Step 4: Delegate in `main.cpp`**

Delete the `PLAYER_MAGIC`/`PLAYER_VERSION` constants and the bodies of `savePlayer`/`loadPlayer`; replace with:

```cpp
#include "PlayerSave.h"
...
void savePlayer() {
    PlayerState s;
    s.pos = app.player.pos;
    s.yaw = app.player.yaw;
    s.pitch = app.player.pitch;
    s.flying = app.player.flying;
    s.hotbarSlot = uint8_t(app.hotbarSlot);
    s.inv = app.inv;
    if (!savePlayerFile(playerPath(), s))
        std::fprintf(stderr, "warning: failed to save player data\n");
}

bool loadPlayer() {
    PlayerState s;
    if (!loadPlayerFile(playerPath(), s)) {
        if (std::ifstream(playerPath()))
            std::fprintf(stderr, "warning: bad/old player save, starting at spawn\n");
        return false;
    }
    app.player.pos = s.pos;
    app.player.prevPos = s.pos;
    app.player.yaw = s.yaw;
    app.player.pitch = s.pitch;
    app.player.flying = s.flying;
    app.hotbarSlot = s.hotbarSlot < HOTBAR_SLOTS ? s.hotbarSlot : 0;
    app.inv = s.inv;
    return true;
}
```

- [ ] **Step 5: Build + tests; manual save check**

`cmake --build build -j && ./build/world_tests` ⇒ PASS. The user's existing `saves/world1/player.bin` is v1 — run `./build/minecraft --frames 60` against the real save and confirm stderr has **no** "bad/old player save" warning and the game starts at the saved position (their real data must migrate, not regenerate). The autosave then rewrites it as v2.

- [ ] **Step 6: Commit**

```bash
git add src/PlayerSave.h src/main.cpp tests/test_world.cpp
git commit -m "feat: player save v2 — inventory persistence, v1 migration"
```

---

### Task 9: Docs, golden check, final verification

**Files:**
- Modify: `TODO.md` (tick Batch G boxes, mark header DONE)
- Modify: `README.md` (controls: E; settings: `survival`; How-it-works: tick/entities/inventory)
- Modify: `docs/handoff/STATUS.md` (Batch G implementation notes + verification snapshot)
- Modify: `docs/handoff/VERIFICATION.md` (add `--demo-items` / `--demo-inv` screenshot recipes)

- [ ] **Step 1: Full verification sweep (per CLAUDE.md + WORKFLOW.md)**

```sh
cmake --build build -j                       # warning-free
for i in 1 2 3; do ./build/world_tests; done # repeat runs
ctest --test-dir build                       # incl. golden screenshot — must pass
./build/minecraft --bench 1000               # fps comparable to Batch F (~340)
./build/minecraft --demo-items --frames 300  # screenshot: bobbing item cubes
./build/minecraft --demo-inv --frames 120    # screenshot: inventory UI
```

TSAN pass (threading wasn't touched, but ticks interleave with the existing pipelines — cheap insurance): rebuild with TSAN per `docs/handoff/VERIFICATION.md` (ASLR workaround) and run `world_tests`.

Interactive survival session on the real display: break a block → item pops out, bobs, magnetizes, count appears in hotbar; place until a stack empties → placement stops; E inventory stack-moving; quit + relaunch → inventory restored.

Clean up all `test_*` save dirs and any temporary saves created during verification (never touch `saves/`).

- [ ] **Step 2: Update TODO.md** — tick all six Batch G boxes, retitle to `## Batch G — Items, Inventory & Entity Foundation — DONE`, note the mode rule (creative = no drops) and the non-persistence of entities.

- [ ] **Step 3: Update README.md** — controls table (`E — inventory (survival)`), settings table (`survival`), short How-it-works paragraphs: 20 TPS tick + interpolation, entity system, drops/pickup, inventory + save v2.

- [ ] **Step 4: Update STATUS.md** — Batch G implementation notes (decisions: shared Body physics, freeze-in-unloaded-chunks, entomb caveat, no entity persistence) + verification snapshot. Update "What's next" to Batch H.

- [ ] **Step 5: Final commit**

```bash
git add TODO.md README.md docs/handoff/STATUS.md docs/handoff/VERIFICATION.md docs/plans/batch-g-items-entities.md
git commit -m "Batch G: fixed tick, item entities, survival inventory, save v2"
```

---

## Self-review notes

- **Spec coverage:** fixed tick → Task 2; entity system (position/velocity/AABB-reuse/update+render lists/per-chunk buckets) → Tasks 1, 4, 6; block drops (bobbing cube, magnetized pickup) → Tasks 4–6; hotbar counts + finite placement + mode flag → Task 5; inventory grid UI → Task 7; persistence v2 → Task 8. All six checkboxes have tasks.
- **Type consistency:** `Body{pos, vel, halfWidth, height, onGround}` everywhere; `Entities::tick(const World&, playerPos, Inventory*, dt)`; `ItemStack{block, count}`; `PlayerState` field names match main.cpp usage.
- **Open verification points flagged inline:** `isAreaReady(pos, 0)` semantics (Task 4), `Shader.h` API for ItemRenderer (Task 6), `atomicSave` signature (Task 8), argv loop restructure for value-less flags (Task 6).
