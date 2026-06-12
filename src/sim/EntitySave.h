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

enum class EntityChunkLoadStatus {
    Missing,
    Loaded,
    Rejected,
};

std::string entityChunkPath(const std::string& saveDir, ChunkKey key);
EntityChunkLoadStatus loadEntityChunkFile(const std::string& path,
                                          ChunkKey expectedKey,
                                          std::vector<SavedDroppedItem>& out);
bool saveEntityChunkFile(const std::string& path,
                         const std::vector<SavedDroppedItem>& items);
bool deleteEntityChunkFile(const std::string& path);
