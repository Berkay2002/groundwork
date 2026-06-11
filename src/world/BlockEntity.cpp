#include "world/BlockEntity.h"

#include "platform/SaveIO.h"
#include "sim/ItemSave.h"
#include <cstdint>
#include <cstring>
#include <fstream>

namespace {
constexpr char BE_MAGIC[4] = {'M', 'C', 'B', 'E'};
constexpr uint32_t BE_VERSION = 1;
constexpr int RAW_IRON_COOK_TICKS = 200;

bool canSmelt(const FurnaceState& f) {
    if (f.input.empty() || f.input.item != ItemId::RawIron) return false;
    if (f.output.empty()) return true;
    return f.output.item == ItemId::IronIngot &&
           f.output.count < itemDef(ItemId::IronIngot).stackMax;
}

void finishSmelt(FurnaceState& f) {
    if (f.output.empty()) f.output = makeItemStack(ItemId::IronIngot, 1);
    else ++f.output.count;
    if (--f.input.count == 0) f.input = {};
}

bool consumeFuelIfNeeded(FurnaceState& f) {
    if (f.burnTicksRemaining > 0) return true;
    if (f.fuel.empty() || itemDef(f.fuel.item).fuelTicks <= 0) return false;
    f.burnTicksRemaining += itemDef(f.fuel.item).fuelTicks;
    if (--f.fuel.count == 0) f.fuel = {};
    return true;
}
}

FurnaceState& BlockEntityStore::getOrCreateFurnace(glm::ivec3 pos) {
    return furnaces_[blockPos(pos)];
}

FurnaceState* BlockEntityStore::furnaceAt(glm::ivec3 pos) {
    auto it = furnaces_.find(blockPos(pos));
    return it == furnaces_.end() ? nullptr : &it->second;
}

const FurnaceState* BlockEntityStore::furnaceAt(glm::ivec3 pos) const {
    auto it = furnaces_.find(blockPos(pos));
    return it == furnaces_.end() ? nullptr : &it->second;
}

void BlockEntityStore::removeFurnace(glm::ivec3 pos) {
    furnaces_.erase(blockPos(pos));
}

void BlockEntityStore::tickFurnaces() {
    for (auto& it : furnaces_) {
        FurnaceState& f = it.second;
        bool lit = consumeFuelIfNeeded(f);
        if (lit && f.burnTicksRemaining > 0) --f.burnTicksRemaining;

        if (!canSmelt(f)) {
            f.cookTicks = 0;
            continue;
        }
        if (!lit) {
            f.cookTicks = 0;
            continue;
        }
        ++f.cookTicks;
        if (f.cookTicks >= RAW_IRON_COOK_TICKS) {
            finishSmelt(f);
            f.cookTicks = 0;
        }
    }
}

bool loadBlockEntitiesFile(const std::string& path, BlockEntityStore& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4];
    uint32_t version = 0, count = 0;
    f.read(magic, 4);
    f.read(reinterpret_cast<char*>(&version), 4);
    f.read(reinterpret_cast<char*>(&count), 4);
    if (!f || std::memcmp(magic, BE_MAGIC, 4) != 0 || version != BE_VERSION)
        return false;

    BlockEntityStore loaded;
    for (uint32_t i = 0; i < count; ++i) {
        int32_t x = 0, y = 0, z = 0;
        FurnaceState state;
        f.read(reinterpret_cast<char*>(&x), 4);
        f.read(reinterpret_cast<char*>(&y), 4);
        f.read(reinterpret_cast<char*>(&z), 4);
        if (!readItemStack(f, state.input) || !readItemStack(f, state.fuel) ||
            !readItemStack(f, state.output)) {
            return false;
        }
        f.read(reinterpret_cast<char*>(&state.burnTicksRemaining), 4);
        f.read(reinterpret_cast<char*>(&state.cookTicks), 4);
        if (!f) return false;
        if (state.burnTicksRemaining < 0) state.burnTicksRemaining = 0;
        if (state.cookTicks < 0) state.cookTicks = 0;
        loaded.getOrCreateFurnace({x, y, z}) = state;
    }
    out = std::move(loaded);
    return true;
}

bool saveBlockEntitiesFile(const std::string& path, const BlockEntityStore& store) {
    return atomicSave(path, [&](std::ofstream& f) {
        uint32_t count = uint32_t(store.furnaceCount());
        f.write(BE_MAGIC, 4);
        f.write(reinterpret_cast<const char*>(&BE_VERSION), 4);
        f.write(reinterpret_cast<const char*>(&count), 4);
        for (const auto& it : store.furnaces()) {
            const BlockPos& p = it.first;
            const FurnaceState& state = it.second;
            int32_t x = p.x, y = p.y, z = p.z;
            f.write(reinterpret_cast<const char*>(&x), 4);
            f.write(reinterpret_cast<const char*>(&y), 4);
            f.write(reinterpret_cast<const char*>(&z), 4);
            writeItemStack(f, state.input);
            writeItemStack(f, state.fuel);
            writeItemStack(f, state.output);
            f.write(reinterpret_cast<const char*>(&state.burnTicksRemaining), 4);
            f.write(reinterpret_cast<const char*>(&state.cookTicks), 4);
        }
    });
}
