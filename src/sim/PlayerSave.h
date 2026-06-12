#pragma once
#include "sim/Inventory.h"
#include "sim/ItemSave.h"
#include "platform/SaveIO.h"
#include <cstring>
#include <fstream>
#include <glm/glm.hpp>
#include <string>

// Player persistence, v4: the v1 fields + 36 item inventory slots (9 cols x 4
// rows; u16 item id, u8 count, u16 durability). v1 files load with an empty
// inventory. v2 block inventories and v3 item inventories (32 slots, 8 cols)
// migrate: old slot r*8+c lands at new slot r*9+c, column 8 of each row is
// left empty.
struct PlayerState {
    glm::vec3 pos{0.5f, 50.0f, 0.5f};
    float yaw = -90.0f, pitch = 0.0f;
    bool flying = false;
    uint8_t hotbarSlot = 0;
    Inventory inv;
};

constexpr char PLAYER_MAGIC[4] = {'M', 'C', 'P', 'L'};
constexpr uint32_t PLAYER_VERSION = 4;

inline ItemId migrateV2BlockItem(uint8_t b) {
    if (b >= BLOCK_TYPES) return ItemId::None;
    Block block = Block(b);
    if (block == Block::Stone) return ItemId::CobblestoneBlock;
    return itemForBlock(block);
}

// v1-v3 files store 32 slots in an 8-column layout; v4 is 9 columns. Keep
// each item's row/column, leaving column 8 of every row empty.
inline int remapSlot8to9(int i) { return (i / 8) * 9 + (i % 8); }

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
        for (int i = 0; i < Inventory::SLOTS; ++i)
            writeItemStack(f, s.inv.slots[i]);
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
        (version != 1 && version != 2 && version != 3 && version != 4))
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
        // v2: 32 slots in an 8-column layout; migrate to 9-column (r*9+c).
        for (int i = 0; i < 32; ++i) {
            uint8_t b = 0, c = 0;
            f.read(reinterpret_cast<char*>(&b), 1);
            f.read(reinterpret_cast<char*>(&c), 1);
            if (!f) return false;
            ItemId item = migrateV2BlockItem(b);
            s.inv.slots[remapSlot8to9(i)] =
                sanitizeLoadedItemStack(uint16_t(item), c, 0);
        }
    } else if (version == 3) {
        // v3: 32 slots in an 8-column layout; migrate to 9-column (r*9+c).
        for (int i = 0; i < 32; ++i) {
            ItemStack stack{};
            if (!readItemStack(f, stack)) return false;
            s.inv.slots[remapSlot8to9(i)] = stack;
        }
    } else if (version == 4) {
        for (int i = 0; i < Inventory::SLOTS; ++i) {
            if (!readItemStack(f, s.inv.slots[i])) return false;
        }
    }
    return true;
}
