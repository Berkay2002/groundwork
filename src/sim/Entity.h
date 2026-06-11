#pragma once
#include "world/Block.h"
#include "sim/Inventory.h"
#include "sim/Physics.h"
#include "world/World.h" // ChunkKey/ChunkKeyHash; World is GL-free until upload
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
