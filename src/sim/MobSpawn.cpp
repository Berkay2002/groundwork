#include "sim/MobSpawn.h"

#include "world/DayCycle.h"
#include <cmath>

namespace {
bool emptyForMob(Block b) {
    return !isSolid(b) && !isWater(b);
}
}

bool darkEnoughForHostile(const World& world, const glm::ivec3& feet,
                          uint8_t maxSpawnLight) {
    if (world.blockLightAt(feet.x, feet.y, feet.z) > maxSpawnLight) return false;
    bool night = dayFactor(dayFraction(world.dayTime())) < 0.05f;
    return night || world.sunLightAt(feet.x, feet.y, feet.z) <= maxSpawnLight;
}

float MobSpawnSystem::rand01() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return float(rng_ >> 8) / float(1u << 24);
}

LivingEntityId MobSpawnSystem::tick(const World& world, Entities& entities,
                                    const glm::vec3& playerPos) {
    // Population cap first — it is the cheap global gate.
    if (int(entities.livingNear(playerPos, rules_.capRadius).size()) >=
        rules_.mobCap)
        return 0;

    float angle = rand01() * 6.2831853f;
    float radius = rules_.minRadius + rand01() * (rules_.maxRadius - rules_.minRadius);
    int wx = int(std::floor(playerPos.x + std::cos(angle) * radius));
    int wz = int(std::floor(playerPos.z + std::sin(angle) * radius));
    int wy = 1 + int(rand01() * float(CHUNK_HEIGHT - 4)); // feet block

    glm::vec3 spawnPos(float(wx) + 0.5f, float(wy), float(wz) + 0.5f);
    if (!world.isAreaReady(spawnPos, 0)) return 0;

    // Ground and headroom: solid non-water footing, two clear blocks above.
    Block ground = world.getBlock(wx, wy - 1, wz);
    if (!isSolid(ground) || isWater(ground)) return 0;
    if (!emptyForMob(world.getBlock(wx, wy, wz))) return 0;
    if (!emptyForMob(world.getBlock(wx, wy + 1, wz))) return 0;

    // The ring pick keeps the horizontal distance in range; the y roll can
    // still land too close overhead or underfoot, so re-check in 3D.
    float dist = glm::distance(spawnPos, playerPos);
    if (dist < rules_.minRadius || dist > rules_.maxRadius) return 0;

    // The only mob today is hostile; when passive kinds exist, pick the kind
    // first and apply the darkness rule only to hostiles.
    MobKind kind = MobKind::Zombie;
    if (mobDef(kind).hostile &&
        !darkEnoughForHostile(world, glm::ivec3(wx, wy, wz), rules_.maxSpawnLight))
        return 0;

    return entities.spawnLiving(spawnPos, kind, SpawnReason::Natural);
}
