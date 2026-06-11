#pragma once

#include "sim/Item.h"
#include <glm/glm.hpp>
#include <cstddef>
#include <string>
#include <unordered_map>

struct BlockPos {
    int x = 0, y = 0, z = 0;
    bool operator==(const BlockPos& o) const {
        return x == o.x && y == o.y && z == o.z;
    }
};

struct BlockPosHash {
    size_t operator()(const BlockPos& p) const {
        uint64_t h = uint32_t(p.x);
        h = h * 0x9E3779B185EBCA87ull + uint32_t(p.y);
        h = h * 0x9E3779B185EBCA87ull + uint32_t(p.z);
        return size_t(h ^ (h >> 32));
    }
};

inline BlockPos blockPos(glm::ivec3 p) { return {p.x, p.y, p.z}; }

struct FurnaceState {
    ItemStack input;
    ItemStack fuel;
    ItemStack output;
    int burnTicksRemaining = 0;
    int cookTicks = 0;
};

inline bool isFurnaceSmeltableInput(ItemId item) {
    return item == ItemId::RawIron;
}

class BlockEntityStore {
public:
    FurnaceState& getOrCreateFurnace(glm::ivec3 pos);
    FurnaceState* furnaceAt(glm::ivec3 pos);
    const FurnaceState* furnaceAt(glm::ivec3 pos) const;
    void removeFurnace(glm::ivec3 pos);
    size_t furnaceCount() const { return furnaces_.size(); }
    void tickFurnaces();

    const std::unordered_map<BlockPos, FurnaceState, BlockPosHash>& furnaces() const {
        return furnaces_;
    }
    void clear() { furnaces_.clear(); }

private:
    std::unordered_map<BlockPos, FurnaceState, BlockPosHash> furnaces_;
};

bool loadBlockEntitiesFile(const std::string& path, BlockEntityStore& out);
bool saveBlockEntitiesFile(const std::string& path, const BlockEntityStore& store);
