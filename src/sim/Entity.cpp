#include "sim/Entity.h"
#include "sim/EntitySave.h"
#include "sim/Player.h"
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
constexpr float LIVING_CHASE_SPEED = 2.2f;
constexpr float LIVING_JUMP_SPEED = 8.0f;     // clears a 1-block step
constexpr float LIVING_CHASE_RADIUS = 12.0f;
constexpr float LIVING_ATTACK_RANGE = 1.6f;
constexpr int LIVING_ATTACK_DAMAGE = 2;       // one heart per bite
constexpr uint32_t LIVING_ATTACK_COOLDOWN = 20; // 1 s at 20 TPS
constexpr uint32_t LIVING_HURT_TICKS = 6;     // knockback steering pause
constexpr uint32_t AMBIENT_SPAWN_CHANCE = 72; // about 28% of eligible chunks

ChunkKey chunkForPos(glm::vec3 pos) {
    return {World::floorDiv((int)std::floor(pos.x), CHUNK_SIZE),
            World::floorDiv((int)std::floor(pos.z), CHUNK_SIZE)};
}

bool sameChunk(glm::vec3 pos, ChunkKey key) {
    return chunkForPos(pos) == key;
}

uint32_t hashAmbient(int32_t x, int32_t z, uint32_t seed) {
    uint32_t h = seed ^ 0xA53C9E7Du;
    h ^= uint32_t(x) * 0x85EBCA6Bu;
    h = (h << 13) | (h >> 19);
    h ^= uint32_t(z) * 0xC2B2AE35u;
    h *= 0x27D4EB2Fu;
    h ^= h >> 15;
    return h;
}

bool emptyForLiving(Block b) {
    return !isSolid(b) && !isWater(b);
}

// Coarse voxel line-of-sight: samples the segment every half block. Good
// enough for "can the zombie see the player" — it does not need to be a
// watertight DDA.
bool hasLineOfSight(const World& world, glm::vec3 from, glm::vec3 to) {
    glm::vec3 d = to - from;
    float len = glm::length(d);
    if (len < 1e-4f) return true;
    d /= len;
    for (float t = 0.5f; t < len; t += 0.5f) {
        glm::vec3 p = from + d * t;
        if (isSolid(world.getBlock((int)std::floor(p.x), (int)std::floor(p.y),
                                   (int)std::floor(p.z))))
            return false;
    }
    return true;
}

bool findAmbientLivingSpawn(const World& world, ChunkKey key, glm::vec3& out) {
    glm::vec3 center(float(key.x * CHUNK_SIZE) + 0.5f, 50.0f,
                     float(key.z * CHUNK_SIZE) + 0.5f);
    if (!world.isAreaReady(center, 0)) return false;

    uint32_t h = hashAmbient(key.x, key.z, world.seed());
    if ((h & 0xFFu) >= AMBIENT_SPAWN_CHANCE) return false;

    for (int attempt = 0; attempt < 8; ++attempt) {
        uint32_t a = hashAmbient(key.x * 17 + attempt, key.z * 31 - attempt,
                                 world.seed() ^ 0xC2EAD123u);
        int wx = key.x * CHUNK_SIZE + 1 + int((a >> 8) % (CHUNK_SIZE - 2));
        int wz = key.z * CHUNK_SIZE + 1 + int((a >> 18) % (CHUNK_SIZE - 2));
        for (int y = CHUNK_HEIGHT - 3; y >= 1; --y) {
            Block ground = world.getBlock(wx, y, wz);
            if (!isSolid(ground) || isWater(ground)) continue;
            if (!emptyForLiving(world.getBlock(wx, y + 1, wz))) continue;
            if (!emptyForLiving(world.getBlock(wx, y + 2, wz))) continue;
            out = glm::vec3(float(wx) + 0.5f, float(y) + 1.0f, float(wz) + 0.5f);
            return true;
        }
    }
    return false;
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

void Entities::spawnAmbientLivingForChunk(const World& world, ChunkKey key) {
    if (ambientLivingChunks_.count(key)) return;
    if (ambientConsumedChunks_.count(key)) return; // already spawned, ever
    glm::vec3 center(float(key.x * CHUNK_SIZE) + 0.5f, 50.0f,
                     float(key.z * CHUNK_SIZE) + 0.5f);
    if (!world.isAreaReady(center, 0)) return;
    ambientLivingChunks_.insert(key);
    glm::vec3 pos;
    if (!findAmbientLivingSpawn(world, key, pos)) return;
    LivingEntityId id = spawnLiving(pos, DEFAULT_CREATURE_MODEL_ID);
    for (auto& up : living_) {
        if (up->id != id) continue;
        up->ambient = true;
        up->homeChunk = key;
        break;
    }
    ambientConsumedChunks_.insert(key);
}

bool Entities::damageLiving(LivingEntityId id, int amount,
                            const glm::vec3& knockDir) {
    if (amount <= 0) return false;
    for (auto& up : living_) {
        LivingEntity& e = *up;
        if (e.id != id || e.dead) continue;
        e.health -= amount;
        if (e.health <= 0) {
            e.dead = true;
            spawnItem(e.body.pos + glm::vec3(0.0f, 0.4f, 0.0f),
                      glm::vec3(0.0f, 2.5f, 0.0f), ItemId::RottenFlesh, 1);
            cleanupLiving();
            rebuildBuckets();
            return true;
        }
        glm::vec3 flat(knockDir.x, 0.0f, knockDir.z);
        float len = glm::length(flat);
        if (len > 1e-4f) {
            flat /= len;
            e.body.vel.x = flat.x * 6.0f;
            e.body.vel.z = flat.z * 6.0f;
            e.body.vel.y = 4.0f;
            e.hurtTicks = LIVING_HURT_TICKS;
        }
        return true;
    }
    return false;
}

LivingEntity* Entities::raycastLiving(const glm::vec3& origin, const glm::vec3& dir,
                                      float maxDist, float* outDist) const {
    LivingEntity* best = nullptr;
    float bestT = maxDist;
    for (LivingEntity* e : livingNear(origin, maxDist + 2.0f)) {
        glm::vec3 lo = e->body.pos - glm::vec3(e->body.halfWidth, 0.0f, e->body.halfWidth);
        glm::vec3 hi = e->body.pos + glm::vec3(e->body.halfWidth, e->body.height,
                                               e->body.halfWidth);
        // Slab test.
        float tmin = 0.0f, tmax = bestT;
        bool miss = false;
        for (int axis = 0; axis < 3 && !miss; ++axis) {
            float o = origin[axis], d = dir[axis];
            if (std::fabs(d) < 1e-8f) {
                if (o < lo[axis] || o > hi[axis]) miss = true;
                continue;
            }
            float t0 = (lo[axis] - o) / d, t1 = (hi[axis] - o) / d;
            if (t0 > t1) std::swap(t0, t1);
            tmin = std::max(tmin, t0);
            tmax = std::min(tmax, t1);
            if (tmin > tmax) miss = true;
        }
        if (!miss && tmin < bestT) {
            bestT = tmin;
            best = e;
        }
    }
    if (best != nullptr && outDist != nullptr) *outDist = bestT;
    return best;
}

void Entities::tick(const World& world, const glm::vec3& playerPos, Inventory* inv,
                    float dt, Player* targetPlayer) {
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

    const bool playerAlive = targetPlayer != nullptr && targetPlayer->health > 0;
    for (auto& up : living_) {
        LivingEntity& e = *up;
        if (e.dead) continue;
        if (!world.isAreaReady(e.body.pos, 1)) continue;
        e.prevPos = e.body.pos;
        if (e.ageTicks < UINT32_MAX) ++e.ageTicks;
        if (e.attackCooldownTicks > 0) --e.attackCooldownTicks;
        if (e.ageTicks > 0 && e.ageTicks % 40 == 0) ++e.movePhase;

        glm::vec3 toPlayer = playerPos - e.body.pos;
        float playerDist = glm::length(toPlayer);
        bool chasing = false;
        if (playerAlive && playerDist < LIVING_CHASE_RADIUS) {
            glm::vec3 eyeFrom = e.body.pos + glm::vec3(0.0f, e.body.height * 0.85f, 0.0f);
            glm::vec3 eyeTo = playerPos + glm::vec3(0.0f, 0.9f, 0.0f); // torso
            chasing = hasLineOfSight(world, eyeFrom, eyeTo);
        }

        if (e.hurtTicks > 0) {
            // Knocked back: keep the impulse, just bleed it off.
            --e.hurtTicks;
            e.body.vel.x *= 0.85f;
            e.body.vel.z *= 0.85f;
        } else if (chasing) {
            glm::vec3 flat(toPlayer.x, 0.0f, toPlayer.z);
            float flatLen = glm::length(flat);
            if (flatLen > 1e-4f) flat /= flatLen;
            e.body.vel.x = flat.x * LIVING_CHASE_SPEED;
            e.body.vel.z = flat.z * LIVING_CHASE_SPEED;
            // Hop over a 1-block step when running into a wall.
            if (e.body.hitWall && e.body.onGround)
                e.body.vel.y = LIVING_JUMP_SPEED;
            if (playerDist < LIVING_ATTACK_RANGE && e.attackCooldownTicks == 0) {
                e.attackCooldownTicks = LIVING_ATTACK_COOLDOWN;
                if (targetPlayer->damage(LIVING_ATTACK_DAMAGE))
                    targetPlayer->applyKnockback(flat * 6.0f +
                                                 glm::vec3(0.0f, 4.0f, 0.0f));
            }
        } else {
            float angle = float(e.movePhase % 4u) * 1.5707963f;
            e.body.vel.x = std::cos(angle) * LIVING_WALK_SPEED;
            e.body.vel.z = std::sin(angle) * LIVING_WALK_SPEED;
        }
        float horizontalSpeed2 = e.body.vel.x * e.body.vel.x + e.body.vel.z * e.body.vel.z;
        if (horizontalSpeed2 > 0.0001f)
            e.facingYaw = std::atan2(e.body.vel.z, e.body.vel.x);
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

SavedEntityChunk Entities::gatherChunk(ChunkKey key) const {
    SavedEntityChunk out;
    for (const auto& up : items_) {
        const ItemEntity& e = *up;
        if (e.dead || !sameChunk(e.body.pos, key)) continue;
        out.items.push_back({e.body.pos, e.body.vel, e.ageTicks, e.spinSeed, e.stack});
    }
    for (const auto& up : living_) {
        const LivingEntity& e = *up;
        if (e.dead || !sameChunk(e.body.pos, key)) continue;
        SavedLivingEntity s;
        s.pos = e.body.pos;
        s.vel = e.body.vel;
        s.health = e.health;
        s.ageTicks = e.ageTicks;
        s.movePhase = e.movePhase;
        s.facingYaw = e.facingYaw;
        s.ambient = e.ambient;
        s.homeChunk = e.homeChunk;
        s.modelId = e.modelId;
        out.living.push_back(std::move(s));
    }
    out.ambientSpawnConsumed = ambientConsumedChunks_.count(key) != 0;
    return out;
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
    SavedEntityChunk saved;
    EntityChunkLoadStatus status =
        loadEntityChunkFile(entityChunkPath(saveDir, key), key, saved);
    if (status == EntityChunkLoadStatus::Rejected) {
        std::fprintf(stderr,
                     "warning: entity chunk %d,%d has bad/old save format, discarding\n",
                     key.x, key.z);
    }
    for (const SavedDroppedItem& s : saved.items) {
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
    for (const SavedLivingEntity& s : saved.living) {
        auto e = std::make_unique<LivingEntity>();
        e->id = nextLivingId_++;
        e->body.pos = s.pos;
        e->body.vel = s.vel;
        e->body.halfWidth = LIVING_HALF_WIDTH;
        e->body.height = LIVING_HEIGHT;
        e->prevPos = s.pos;
        e->modelId = s.modelId;
        e->health = s.health;
        e->ageTicks = s.ageTicks;
        e->movePhase = s.movePhase;
        e->facingYaw = s.facingYaw;
        e->ambient = s.ambient;
        e->homeChunk = s.homeChunk;
        living_.push_back(std::move(e));
    }
    if (saved.ambientSpawnConsumed) {
        ambientConsumedChunks_.insert(key);
        ambientLivingChunks_.insert(key);
    }
    loadedEntityChunks_.insert(key);
    rebuildBuckets();
}

bool Entities::saveLoadedChunkEntities(const std::string& saveDir, ChunkKey key,
                                       bool saveEnabled) {
    if (!loadedEntityChunks_.count(key)) return true;
    if (!saveEnabled) return true;
    bool ok = saveEntityChunkFile(entityChunkPath(saveDir, key), gatherChunk(key));
    if (!ok)
        std::fprintf(stderr, "warning: failed to save entity chunk %d,%d\n",
                     key.x, key.z);
    return ok;
}

void Entities::saveAndUnloadChunkEntities(const std::string& saveDir, ChunkKey key,
                                          bool saveEnabled) {
    if (saveEnabled) {
        if (!saveEntityChunkFile(entityChunkPath(saveDir, key), gatherChunk(key)))
            std::fprintf(stderr, "warning: failed to save entity chunk %d,%d\n",
                         key.x, key.z);
    }
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                     [&](const std::unique_ptr<ItemEntity>& e) {
                         return sameChunk(e->body.pos, key);
                     }),
                 items_.end());
    living_.erase(std::remove_if(living_.begin(), living_.end(),
                      [&](const std::unique_ptr<LivingEntity>& e) {
                          return sameChunk(e->body.pos, key);
                      }),
                  living_.end());
    loadedEntityChunks_.erase(key);
    ambientLivingChunks_.erase(key);
    ambientConsumedChunks_.erase(key);
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
                                 bool saveEnabled,
                                 const World* world,
                                 bool spawnAmbientLiving) {
    for (ChunkKey key : events.loaded) {
        loadChunkEntities(saveDir, key);
        if (world != nullptr && spawnAmbientLiving)
            spawnAmbientLivingForChunk(*world, key);
    }
    for (ChunkKey key : events.unloaded) {
        saveAndUnloadChunkEntities(saveDir, key, saveEnabled);
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
