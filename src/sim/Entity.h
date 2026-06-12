#pragma once
#include "world/Block.h"
#include "sim/Inventory.h"
#include "sim/Physics.h"
#include "world/World.h" // ChunkKey/ChunkKeyHash; World is GL-free until upload
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

constexpr uint32_t PICKUP_DELAY_TICKS = 8; // 0.4 s at 20 TPS
constexpr uint32_t DESPAWN_TICKS = 6000;   // 5 min at 20 TPS

// The first entity: a dropped item. A small cube body that falls with the
// shared sub-stepped AABB collision, magnetizes to the player, and is
// collected into the inventory. Bob/spin are render-only (ItemRenderer).
struct ItemEntity {
    Body body;             // halfWidth 0.125, height 0.25
    glm::vec3 prevPos{0};  // previous-tick position for render interpolation
    ItemStack stack;
    uint32_t ageTicks = 0; // active simulation ticks since spawn
    uint32_t spinSeed = 0; // de-syncs bob/spin phase between items
    bool dead = false;
    glm::vec3 renderPos(float alpha) const { return glm::mix(prevPos, body.pos, alpha); }
};

// Entity manager. Main thread only, like the chunk map: workers never see
// entities. Items in unloaded chunks freeze (no physics, no aging) — the
// world there is undefined and they would fall forever. Chunk streaming calls
// load/save/unload methods so inactive chunks do not keep runtime entities.
class Entities {
public:
    void spawnItem(const glm::vec3& pos, const glm::vec3& vel, ItemId item, int count = 1);
    void spawnItem(const glm::vec3& pos, const glm::vec3& vel, ItemStack stack);
    // Toss an item stack from a block center with a small pseudo-random
    // horizontal kick. No-op for empty stacks.
    void spawnBlockDrop(const glm::ivec3& blockPos, ItemStack stack);
    // Correct-harvest drop for old callers that only know the broken block.
    void spawnBlockDrop(const glm::ivec3& blockPos, Block broken);

    // One simulation tick: physics, magnetized pickup into `inv` (skipped
    // when inv is null), despawn, then rebuild the per-chunk buckets.
    void tick(const World& world, const glm::vec3& playerPos, Inventory* inv, float dt);

    void loadChunkEntities(const std::string& saveDir, ChunkKey key);
    bool saveLoadedChunkEntities(const std::string& saveDir, ChunkKey key,
                                 bool saveEnabled);
    void saveAndUnloadChunkEntities(const std::string& saveDir, ChunkKey key,
                                    bool saveEnabled);
    void saveAllLoadedEntityChunks(const std::string& saveDir, bool saveEnabled);
    void applyStreamEvents(const std::string& saveDir,
                           const ChunkStreamEvents& events,
                           bool saveEnabled);

    const std::vector<std::unique_ptr<ItemEntity>>& items() const { return items_; }
    // Bucket-accelerated proximity query (used by tests now, mobs later).
    std::vector<ItemEntity*> itemsNear(const glm::vec3& pos, float radius) const;

private:
    std::vector<std::unique_ptr<ItemEntity>> items_;
    std::unordered_map<ChunkKey, std::vector<ItemEntity*>, ChunkKeyHash> buckets_;
    std::unordered_set<ChunkKey, ChunkKeyHash> loadedEntityChunks_;
    uint32_t rng_ = 0x9E3779B9u;
    float rand01();
    void rebuildBuckets();
};
