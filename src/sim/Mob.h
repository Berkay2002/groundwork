#pragma once
#include "sim/ItemIds.h"
#include <cstdint>

// Mob kinds and spawn reasons are saved bytes (MCEN living records):
// append-only, never renumber. New mobs are new table rows, not subclasses —
// behavior stays in Entities::tick, driven by the def.

enum class MobKind : uint8_t {
    Zombie = 0,
};
constexpr uint8_t MOB_KINDS = 1;

// Why a mob exists. Drives future respawn/despawn policy: ambient mobs are
// the one-shot world population, natural mobs come from MobSpawnSystem.
enum class SpawnReason : uint8_t {
    Staged = 0,  // demo, test, or debug spawns
    Ambient = 1, // deterministic once-per-world chunk population
    Natural = 2, // MobSpawnSystem pacing around the player
};
constexpr uint8_t SPAWN_REASONS = 3;

struct MobDef {
    const char* name;
    const char* modelId;
    int maxHealth;
    bool hostile;
    ItemId dropItem;
    uint8_t dropCount;
};

inline const MobDef& mobDef(MobKind kind) {
    static const MobDef DEFS[MOB_KINDS] = {
        /* 0 Zombie */ {"Zombie", "creature.kenney_zombie_a", 10, true,
                        ItemId::RottenFlesh, 1},
    };
    return DEFS[uint8_t(kind)];
}

inline bool isValidMobKind(uint8_t raw) { return raw < MOB_KINDS; }
inline bool isValidSpawnReason(uint8_t raw) { return raw < SPAWN_REASONS; }
