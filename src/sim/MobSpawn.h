#pragma once
#include "sim/Entity.h"
#include "world/World.h"
#include <glm/glm.hpp>
#include <cstdint>

// Minecraft-style natural mob spawning, separate from Entities::tick so the
// pacing policy stays in one place. One attempt per simulation tick; the
// caller gates on game mode (survival only, live player, no demo runs).
//
// An attempt picks a random spot in a ring around the player and spawns one
// mob there if every rule passes:
//   - the chunk is loaded (generated and integrated),
//   - the spot is outside `minRadius` and inside `maxRadius` of the player,
//   - the living mob count within `capRadius` of the player is under `mobCap`,
//   - the ground block is solid and not water, with two non-solid,
//     non-water blocks above it,
//   - hostiles need darkness: block light at most `maxSpawnLight`, and
//     either night or a spot sunlight cannot reach (caves spawn by day).
struct MobSpawnRules {
    float minRadius = 24.0f;
    float maxRadius = 96.0f;
    float capRadius = 128.0f;
    int mobCap = 10;
    uint8_t maxSpawnLight = 3;
};

// True where a hostile mob may spawn given the darkness rule above.
bool darkEnoughForHostile(const World& world, const glm::ivec3& feet,
                          uint8_t maxSpawnLight);

class MobSpawnSystem {
public:
    explicit MobSpawnSystem(MobSpawnRules rules = {}, uint32_t seed = 0x51F15EEDu)
        : rules_(rules), rng_(seed) {}

    // One spawn attempt. Returns the spawned id, or 0 if any rule failed.
    LivingEntityId tick(const World& world, Entities& entities,
                        const glm::vec3& playerPos);

    const MobSpawnRules& rules() const { return rules_; }

private:
    MobSpawnRules rules_;
    uint32_t rng_;
    float rand01();
};
