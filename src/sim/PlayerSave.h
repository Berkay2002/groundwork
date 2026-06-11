#pragma once
#include "sim/Inventory.h"
#include "platform/SaveIO.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <glm/glm.hpp>
#include <string>

// Player persistence, v3: the v1 fields + 32 item inventory slots
// (u16 item id, u8 count, u16 durability). v1 files still load with an
// empty inventory. v2 block inventories migrate slot-for-slot.
struct PlayerState {
    glm::vec3 pos{0.5f, 50.0f, 0.5f};
    float yaw = -90.0f, pitch = 0.0f;
    bool flying = false;
    uint8_t hotbarSlot = 0;
    Inventory inv;
};

constexpr char PLAYER_MAGIC[4] = {'M', 'C', 'P', 'L'};
constexpr uint32_t PLAYER_VERSION = 3;

inline ItemId migrateV2BlockItem(uint8_t b) {
    if (b >= BLOCK_TYPES) return ItemId::None;
    Block block = Block(b);
    if (block == Block::Stone) return ItemId::CobblestoneBlock;
    return itemForBlock(block);
}

inline ItemStack sanitizeLoadedStack(uint16_t rawId, uint8_t count,
                                     uint16_t durability) {
    if (rawId >= ITEM_TYPES || count == 0) return {};
    ItemId id = ItemId(rawId);
    if (id == ItemId::None) return {};
    const ItemDef& d = itemDef(id);
    if (d.maxDurability > 0) {
        if (durability == 0) return {};
        if (durability > d.maxDurability) durability = d.maxDurability;
        return {id, 1, durability};
    }
    return {id, uint8_t(std::min<int>(count, d.stackMax)), 0};
}

inline bool savePlayerFile(const std::string& path, const PlayerState& s) {
    return atomicSave(path, [&](std::ofstream& f) {
        f.write(PLAYER_MAGIC, 4);
        f.write(reinterpret_cast<const char*>(&PLAYER_VERSION), 4);
        f.write(reinterpret_cast<const char*>(&s.pos), sizeof(s.pos));
        f.write(reinterpret_cast<const char*>(&s.yaw), sizeof(s.yaw));
        f.write(reinterpret_cast<const char*>(&s.pitch), sizeof(s.pitch));
        uint8_t flying = s.flying ? 1 : 0;
        f.write(reinterpret_cast<const char*>(&flying), 1);
        f.write(reinterpret_cast<const char*>(&s.hotbarSlot), 1);
        for (int i = 0; i < Inventory::SLOTS; ++i) {
            uint16_t item = uint16_t(s.inv.slots[i].item);
            uint8_t c = s.inv.slots[i].count;
            uint16_t durability = s.inv.slots[i].durability;
            f.write(reinterpret_cast<const char*>(&item), 2);
            f.write(reinterpret_cast<const char*>(&c), 1);
            f.write(reinterpret_cast<const char*>(&durability), 2);
        }
    });
}

inline bool loadPlayerFile(const std::string& path, PlayerState& s) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4];
    uint32_t version = 0;
    f.read(magic, 4);
    f.read(reinterpret_cast<char*>(&version), 4);
    if (!f || std::memcmp(magic, PLAYER_MAGIC, 4) != 0 ||
        (version != 1 && version != 2 && version != 3))
        return false;
    f.read(reinterpret_cast<char*>(&s.pos), sizeof(s.pos));
    f.read(reinterpret_cast<char*>(&s.yaw), sizeof(s.yaw));
    f.read(reinterpret_cast<char*>(&s.pitch), sizeof(s.pitch));
    uint8_t flying = 0, slot = 0;
    f.read(reinterpret_cast<char*>(&flying), 1);
    f.read(reinterpret_cast<char*>(&slot), 1);
    if (!f) return false;
    s.flying = flying != 0;
    s.hotbarSlot = slot;
    s.inv = Inventory{};
    if (version == 2) {
        for (int i = 0; i < Inventory::SLOTS; ++i) {
            uint8_t b = 0, c = 0;
            f.read(reinterpret_cast<char*>(&b), 1);
            f.read(reinterpret_cast<char*>(&c), 1);
            if (!f) return false;
            ItemId item = migrateV2BlockItem(b);
            s.inv.slots[i] = item == ItemId::None ? ItemStack{} : ItemStack{item, c, 0};
        }
    } else if (version == 3) {
        for (int i = 0; i < Inventory::SLOTS; ++i) {
            uint16_t item = 0, durability = 0;
            uint8_t c = 0;
            f.read(reinterpret_cast<char*>(&item), 2);
            f.read(reinterpret_cast<char*>(&c), 1);
            f.read(reinterpret_cast<char*>(&durability), 2);
            if (!f) return false;
            s.inv.slots[i] = sanitizeLoadedStack(item, c, durability);
        }
    }
    return true;
}
