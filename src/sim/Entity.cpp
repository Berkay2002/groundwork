#include "sim/Entity.h"
#include "sim/EntitySave.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr float ITEM_GRAVITY = -22.0f;
constexpr float ITEM_TERMINAL = -30.0f;
constexpr float GROUND_FRICTION = 0.6f; // horizontal damping per tick on ground
constexpr float MAGNET_RADIUS = 2.0f;   // starts flying to the player inside this
constexpr float MAGNET_SPEED = 8.0f;
constexpr float PICKUP_RADIUS = 0.8f;
constexpr float MERGE_RADIUS = 0.75f;
constexpr float LIVING_GRAVITY = -22.0f;
constexpr float LIVING_TERMINAL = -30.0f;
constexpr float LIVING_WALK_SPEED = 0.75f;

ChunkKey chunkForPos(glm::vec3 pos) {
    return {World::floorDiv((int)std::floor(pos.x), CHUNK_SIZE),
            World::floorDiv((int)std::floor(pos.z), CHUNK_SIZE)};
}

bool sameChunk(glm::vec3 pos, ChunkKey key) {
    return chunkForPos(pos) == key;
}
}

float Entities::rand01() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return float(rng_ >> 8) / float(1u << 24);
}

void Entities::spawnItem(const glm::vec3& pos, const glm::vec3& vel, ItemId item, int count) {
    if (!isValidItemId(item) || count <= 0) return;
    const ItemDef& d = itemDef(item);
    while (count > 0) {
        int take = std::min(count, int(d.stackMax));
        spawnItem(pos, vel, makeItemStack(item, take));
        count -= take;
        if (d.stackMax <= 0) break;
    }
}

void Entities::spawnItem(const glm::vec3& pos, const glm::vec3& vel, ItemStack stack) {
    int count = stack.count;
    stack = makeItemStack(stack.item, 1, stack.durability);
    if (stack.empty()) return;
    const ItemDef& d = itemDef(stack.item);
    while (count > 0) {
        int take = std::min(count, int(d.stackMax));
        ItemStack part = makeItemStack(stack.item, take, stack.durability);
        if (part.empty()) return;
        auto e = std::make_unique<ItemEntity>();
        e->body.pos = pos;
        e->body.vel = vel;
        e->body.halfWidth = 0.125f;
        e->body.height = 0.25f;
        e->prevPos = pos;
        e->stack = part;
        e->spinSeed = rng_;
        items_.push_back(std::move(e));
        count -= take;
    }
}

void Entities::spawnBlockDrop(const glm::ivec3& blockPos, ItemStack stack) {
    stack = normalizeItemStack(stack);
    if (stack.empty()) return;
    float a = rand01() * 6.2831853f;
    spawnItem(glm::vec3(blockPos) + glm::vec3(0.5f, 0.4f, 0.5f),
              glm::vec3(std::cos(a) * 1.5f, 3.5f, std::sin(a) * 1.5f),
              stack);
}

void Entities::spawnBlockDrop(const glm::ivec3& blockPos, Block broken) {
    const BlockDef& d = blockDef(broken);
    spawnBlockDrop(blockPos, makeItemStack(d.dropItem, d.dropCount));
}

LivingEntityId Entities::spawnLiving(const glm::vec3& pos, const std::string& modelId) {
    if (modelId.empty()) return 0;
    auto e = std::make_unique<LivingEntity>();
    e->id = nextLivingId_++;
    e->body.pos = pos;
    e->body.halfWidth = LIVING_HALF_WIDTH;
    e->body.height = LIVING_HEIGHT;
    e->prevPos = pos;
    e->modelId = modelId;
    LivingEntityId id = e->id;
    living_.push_back(std::move(e));
    rebuildBuckets();
    return id;
}

bool Entities::damageLiving(LivingEntityId id, int amount) {
    if (amount <= 0) return false;
    for (auto& up : living_) {
        LivingEntity& e = *up;
        if (e.id != id || e.dead) continue;
        e.health -= amount;
        if (e.health <= 0) {
            e.dead = true;
            spawnItem(e.body.pos + glm::vec3(0.0f, 0.4f, 0.0f),
                      glm::vec3(0.0f, 2.5f, 0.0f), ItemId::Coal, 1);
            cleanupLiving();
            rebuildBuckets();
        }
        return true;
    }
    return false;
}

void Entities::tick(const World& world, const glm::vec3& playerPos, Inventory* inv, float dt) {
    const glm::vec3 target = playerPos + glm::vec3(0, 0.9f, 0); // player torso
    for (auto& up : items_) {
        ItemEntity& e = *up;
        e.prevPos = e.body.pos;
        if (!world.isAreaReady(e.body.pos, 0)) continue; // frozen in the void
        if (e.ageTicks < UINT32_MAX) ++e.ageTicks;
        glm::vec3 center = e.body.pos + glm::vec3(0, e.body.height * 0.5f, 0);
        bool canPick = e.ageTicks >= PICKUP_DELAY_TICKS && inv != nullptr;
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
            int leftover = inv->addStack(e.stack);
            if (leftover == 0) e.dead = true;
            else e.stack.count = uint8_t(leftover); // inventory full: keep the remainder
        }
        if (e.ageTicks >= DESPAWN_TICKS) e.dead = true;
    }

    for (size_t i = 0; i < items_.size(); ++i) {
        ItemEntity& a = *items_[i];
        if (a.dead || !world.isAreaReady(a.body.pos, 0)) continue;
        const ItemDef& d = itemDef(a.stack.item);
        if (a.stack.count >= d.stackMax) continue;
        for (size_t j = i + 1; j < items_.size() && a.stack.count < d.stackMax; ++j) {
            ItemEntity& b = *items_[j];
            if (b.dead || !world.isAreaReady(b.body.pos, 0)) continue;
            if (!stacksCompatible(a.stack, b.stack)) continue;
            if (glm::distance(a.body.pos, b.body.pos) > MERGE_RADIUS) continue;
            int space = int(d.stackMax) - int(a.stack.count);
            int moved = std::min(space, int(b.stack.count));
            a.stack.count = uint8_t(a.stack.count + moved);
            b.stack.count = uint8_t(b.stack.count - moved);
            if (b.stack.count == 0) b.dead = true;
        }
    }

    items_.erase(std::remove_if(items_.begin(), items_.end(),
                     [](const std::unique_ptr<ItemEntity>& e) { return e->dead; }),
                 items_.end());

    for (auto& up : living_) {
        LivingEntity& e = *up;
        if (e.dead) continue;
        if (!world.isAreaReady(e.body.pos, 1)) continue;
        e.prevPos = e.body.pos;
        if (e.ageTicks < UINT32_MAX) ++e.ageTicks;
        if (e.ageTicks > 0 && e.ageTicks % 40 == 0) ++e.movePhase;
        float angle = float(e.movePhase % 4u) * 1.5707963f;
        e.body.vel.x = std::cos(angle) * LIVING_WALK_SPEED;
        e.body.vel.z = std::sin(angle) * LIVING_WALK_SPEED;
        e.body.vel.y += LIVING_GRAVITY * dt;
        if (e.body.vel.y < LIVING_TERMINAL) e.body.vel.y = LIVING_TERMINAL;
        moveBody(world, e.body, dt);
    }
    cleanupLiving();
    rebuildBuckets();
}

void Entities::cleanupLiving() {
    living_.erase(std::remove_if(living_.begin(), living_.end(),
                      [](const std::unique_ptr<LivingEntity>& e) { return e->dead; }),
                  living_.end());
}

void Entities::rebuildBuckets() {
    buckets_.clear();
    for (auto& up : items_) {
        buckets_[chunkForPos(up->body.pos)].push_back(up.get());
    }
    livingBuckets_.clear();
    for (auto& up : living_) {
        livingBuckets_[chunkForPos(up->body.pos)].push_back(up.get());
    }
}

void Entities::loadChunkEntities(const std::string& saveDir, ChunkKey key) {
    if (loadedEntityChunks_.count(key)) return;
    std::vector<SavedDroppedItem> saved;
    EntityChunkLoadStatus status =
        loadEntityChunkFile(entityChunkPath(saveDir, key), key, saved);
    if (status == EntityChunkLoadStatus::Rejected) {
        std::fprintf(stderr,
                     "warning: entity chunk %d,%d has bad/old save format, discarding\n",
                     key.x, key.z);
    }
    for (const SavedDroppedItem& s : saved) {
        auto e = std::make_unique<ItemEntity>();
        e->body.pos = s.pos;
        e->body.vel = s.vel;
        e->body.halfWidth = 0.125f;
        e->body.height = 0.25f;
        e->prevPos = s.pos;
        e->stack = s.stack;
        e->ageTicks = s.ageTicks;
        e->spinSeed = s.spinSeed;
        items_.push_back(std::move(e));
    }
    loadedEntityChunks_.insert(key);
    rebuildBuckets();
}

bool Entities::saveLoadedChunkEntities(const std::string& saveDir, ChunkKey key,
                                       bool saveEnabled) {
    if (!loadedEntityChunks_.count(key)) return true;
    if (!saveEnabled) return true;
    std::vector<SavedDroppedItem> saved;
    for (const auto& up : items_) {
        const ItemEntity& e = *up;
        if (!sameChunk(e.body.pos, key)) continue;
        saved.push_back({e.body.pos, e.body.vel, e.ageTicks, e.spinSeed, e.stack});
    }
    bool ok = saveEntityChunkFile(entityChunkPath(saveDir, key), saved);
    if (!ok)
        std::fprintf(stderr, "warning: failed to save entity chunk %d,%d\n",
                     key.x, key.z);
    return ok;
}

void Entities::saveAndUnloadChunkEntities(const std::string& saveDir, ChunkKey key,
                                          bool saveEnabled) {
    if (saveEnabled) {
        std::vector<SavedDroppedItem> saved;
        for (const auto& up : items_) {
            const ItemEntity& e = *up;
            if (!sameChunk(e.body.pos, key)) continue;
            saved.push_back({e.body.pos, e.body.vel, e.ageTicks, e.spinSeed, e.stack});
        }
        if (!saveEntityChunkFile(entityChunkPath(saveDir, key), saved))
            std::fprintf(stderr, "warning: failed to save entity chunk %d,%d\n",
                         key.x, key.z);
    }
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                     [&](const std::unique_ptr<ItemEntity>& e) {
                         return sameChunk(e->body.pos, key);
                     }),
                 items_.end());
    loadedEntityChunks_.erase(key);
    rebuildBuckets();
}

void Entities::saveAllLoadedEntityChunks(const std::string& saveDir,
                                         bool saveEnabled) {
    if (!saveEnabled) return;
    std::vector<ChunkKey> keys(loadedEntityChunks_.begin(), loadedEntityChunks_.end());
    for (ChunkKey key : keys)
        saveLoadedChunkEntities(saveDir, key, true);
}

void Entities::applyStreamEvents(const std::string& saveDir,
                                 const ChunkStreamEvents& events,
                                 bool saveEnabled) {
    for (ChunkKey key : events.loaded)
        loadChunkEntities(saveDir, key);
    for (ChunkKey key : events.unloaded)
        saveAndUnloadChunkEntities(saveDir, key, saveEnabled);
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

std::vector<LivingEntity*> Entities::livingNear(const glm::vec3& pos, float radius) const {
    std::vector<LivingEntity*> out;
    int cx0 = World::floorDiv((int)std::floor(pos.x - radius), CHUNK_SIZE);
    int cx1 = World::floorDiv((int)std::floor(pos.x + radius), CHUNK_SIZE);
    int cz0 = World::floorDiv((int)std::floor(pos.z - radius), CHUNK_SIZE);
    int cz1 = World::floorDiv((int)std::floor(pos.z + radius), CHUNK_SIZE);
    for (int cz = cz0; cz <= cz1; ++cz)
        for (int cx = cx0; cx <= cx1; ++cx) {
            auto it = livingBuckets_.find(ChunkKey{cx, cz});
            if (it == livingBuckets_.end()) continue;
            for (LivingEntity* e : it->second)
                if (!e->dead && glm::distance(e->body.pos, pos) <= radius) out.push_back(e);
        }
    return out;
}
