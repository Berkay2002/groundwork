#include "sim/Entity.h"
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

void Entities::spawnBlockDrop(const glm::ivec3& blockPos, Block broken) {
    Block drop = blockDef(broken).drop;
    if (drop == Block::Air) return;
    float a = rand01() * 6.2831853f;
    spawnItem(glm::vec3(blockPos) + glm::vec3(0.5f, 0.4f, 0.5f),
              glm::vec3(std::cos(a) * 1.5f, 3.5f, std::sin(a) * 1.5f),
              itemForBlock(drop), 1);
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
            int leftover = inv->addStack(e.stack);
            if (leftover == 0) e.dead = true;
            else e.stack.count = uint8_t(leftover); // inventory full: keep the remainder
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
