#pragma once
#include "world/Block.h"
#include "sim/Inventory.h"
#include "sim/Mob.h"
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
constexpr int LIVING_MAX_HEALTH = 10;
constexpr float LIVING_HALF_WIDTH = 0.3f;
constexpr float LIVING_HEIGHT = 1.6f;
constexpr const char* DEFAULT_CREATURE_MODEL_ID = "creature.kenney_zombie_a";

using LivingEntityId = uint32_t;

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

struct LivingEntity {
    LivingEntityId id = 0;
    Body body;
    glm::vec3 prevPos{0.0f};
    std::string modelId;
    MobKind kind = MobKind::Zombie;
    SpawnReason reason = SpawnReason::Staged;
    int health = LIVING_MAX_HEALTH;
    uint32_t ageTicks = 0;
    uint32_t movePhase = 0;
    float facingYaw = 0.0f; // radians; 0 faces +X in simulation space
    ChunkKey homeChunk{0, 0};
    bool dead = false;
    // Runtime-only combat state (not persisted; rederived next session).
    uint32_t attackCooldownTicks = 0;
    uint32_t hurtTicks = 0; // knockback window: AI does not steer while > 0
    glm::vec3 renderPos(float alpha) const { return glm::mix(prevPos, body.pos, alpha); }
};

struct SavedEntityChunk;
class Player;

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

    // Staged spawn with an explicit model (demos/tests). Kind stays Zombie.
    LivingEntityId spawnLiving(const glm::vec3& pos, const std::string& modelId);
    // Table-driven spawn: model, health, and behavior come from mobDef(kind).
    LivingEntityId spawnLiving(const glm::vec3& pos, MobKind kind,
                               SpawnReason reason);
    void spawnAmbientLivingForChunk(const World& world, ChunkKey key);
    // Damage with an optional knockback impulse (horizontal direction of the
    // hit; vertical pop is added internally).
    bool damageLiving(LivingEntityId id, int amount,
                      const glm::vec3& knockDir = glm::vec3(0.0f));
    // First living entity hit by the ray, or null. Used for melee picking;
    // a block between origin and the body still counts as a hit (the block
    // raycast decides which is closer at the call site).
    LivingEntity* raycastLiving(const glm::vec3& origin, const glm::vec3& dir,
                                float maxDist, float* outDist = nullptr) const;

    // One simulation tick: physics, magnetized pickup into `inv` (skipped
    // when inv is null), despawn, then rebuild the per-chunk buckets.
    // `targetPlayer` non-null enables hostile AI: creatures chase a live
    // player within sight and melee when adjacent (pass null for creative
    // mode, demos, and tests that want pure wandering).
    void tick(const World& world, const glm::vec3& playerPos, Inventory* inv,
              float dt, Player* targetPlayer = nullptr);

    void loadChunkEntities(const std::string& saveDir, ChunkKey key);
    bool saveLoadedChunkEntities(const std::string& saveDir, ChunkKey key,
                                 bool saveEnabled);
    void saveAndUnloadChunkEntities(const std::string& saveDir, ChunkKey key,
                                    bool saveEnabled);
    void saveAllLoadedEntityChunks(const std::string& saveDir, bool saveEnabled);
    // Incremental autosave: cycles through all loaded entity chunks once per
    // `intervalSeconds`, saving a few files per call instead of bursting
    // hundreds in one frame. Call once per frame; exit/unload still use the
    // full-save paths above.
    void autosaveTick(const std::string& saveDir, bool saveEnabled, float dt,
                      float intervalSeconds);
    void applyStreamEvents(const std::string& saveDir,
                           const ChunkStreamEvents& events,
                           bool saveEnabled,
                           const World* world = nullptr,
                           bool spawnAmbientLiving = false);

    const std::vector<std::unique_ptr<ItemEntity>>& items() const { return items_; }
    const std::vector<std::unique_ptr<LivingEntity>>& living() const { return living_; }
    // Bucket-accelerated proximity query (used by tests now, mobs later).
    std::vector<ItemEntity*> itemsNear(const glm::vec3& pos, float radius) const;
    std::vector<LivingEntity*> livingNear(const glm::vec3& pos, float radius) const;

private:
    std::vector<std::unique_ptr<ItemEntity>> items_;
    std::unordered_map<ChunkKey, std::vector<ItemEntity*>, ChunkKeyHash> buckets_;
    std::vector<std::unique_ptr<LivingEntity>> living_;
    std::unordered_map<ChunkKey, std::vector<LivingEntity*>, ChunkKeyHash> livingBuckets_;
    // Chunks where the ambient rule already ran this session (suppresses
    // re-running it on repeated load events while the chunk stays active).
    std::unordered_set<ChunkKey, ChunkKeyHash> ambientLivingChunks_;
    // Chunks whose ambient spawn actually produced a creature, ever. Persisted
    // as a marker record in the chunk's entity file so a chunk spawns its
    // ambient creature exactly once per world.
    std::unordered_set<ChunkKey, ChunkKeyHash> ambientConsumedChunks_;
    std::unordered_set<ChunkKey, ChunkKeyHash> loadedEntityChunks_;
    std::vector<ChunkKey> autosaveQueue_; // refilled snapshot of loaded chunks
    float autosaveCredit_ = 0.0f;
    LivingEntityId nextLivingId_ = 1;
    uint32_t rng_ = 0x9E3779B9u;
    float rand01();
    SavedEntityChunk gatherChunk(ChunkKey key) const;
    void cleanupLiving();
    void rebuildBuckets();
};
