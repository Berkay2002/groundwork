#pragma once

#include "sim/Item.h"
#include "world/World.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

struct SavedDroppedItem {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    uint32_t ageTicks = 0;
    uint32_t spinSeed = 0;
    ItemStack stack;
};

struct SavedLivingEntity {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    int32_t health = 0;
    uint32_t ageTicks = 0;
    uint32_t movePhase = 0;
    float facingYaw = 0.0f;
    // Raw saved bytes for MobKind/SpawnReason (sim/Mob.h). Legacy living
    // records (MCEN type 2) stored an ambient flag where `reason` now lives —
    // its 0/1 values map exactly onto Staged/Ambient, and `kind` defaults to
    // Zombie (0).
    uint8_t kind = 0;
    uint8_t reason = 0;
    ChunkKey homeChunk{0, 0};
    std::string modelId;
};

// Everything one chunk's entity file holds. `ambientSpawnConsumed` records
// that this chunk's deterministic ambient spawn already produced a creature,
// so reloading the chunk must not spawn another one — even after that
// creature died or wandered away.
struct SavedEntityChunk {
    std::vector<SavedDroppedItem> items;
    std::vector<SavedLivingEntity> living;
    bool ambientSpawnConsumed = false;

    bool empty() const {
        return items.empty() && living.empty() && !ambientSpawnConsumed;
    }
};

enum class EntityChunkLoadStatus {
    Missing,
    Loaded,
    Rejected,
};

std::string entityChunkPath(const std::string& saveDir, ChunkKey key);
EntityChunkLoadStatus loadEntityChunkFile(const std::string& path,
                                          ChunkKey expectedKey,
                                          SavedEntityChunk& out);
bool saveEntityChunkFile(const std::string& path,
                         const SavedEntityChunk& chunk);
bool deleteEntityChunkFile(const std::string& path);
