#pragma once
#include "sim/Inventory.h"
#include "platform/SaveIO.h"
#include <cstring>
#include <fstream>
#include <glm/glm.hpp>
#include <string>

// Player persistence, v2: the v1 fields + 32 inventory slots (block byte,
// count byte). v1 files still load (with an empty inventory) so existing
// saves keep their position — only unknown magic/version is rejected.
struct PlayerState {
    glm::vec3 pos{0.5f, 50.0f, 0.5f};
    float yaw = -90.0f, pitch = 0.0f;
    bool flying = false;
    uint8_t hotbarSlot = 0;
    Inventory inv;
};

constexpr char PLAYER_MAGIC[4] = {'M', 'C', 'P', 'L'};
constexpr uint32_t PLAYER_VERSION = 2;

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
            uint8_t b = uint8_t(s.inv.slots[i].block);
            uint8_t c = s.inv.slots[i].count;
            f.write(reinterpret_cast<const char*>(&b), 1);
            f.write(reinterpret_cast<const char*>(&c), 1);
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
        (version != 1 && version != 2))
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
    if (version >= 2) {
        for (int i = 0; i < Inventory::SLOTS; ++i) {
            uint8_t b = 0, c = 0;
            f.read(reinterpret_cast<char*>(&b), 1);
            f.read(reinterpret_cast<char*>(&c), 1);
            if (!f) return false;
            if (b >= BLOCK_TYPES) { b = 0; c = 0; } // clamp unknown ids, like chunk load
            s.inv.slots[i] = {Block(b), b == 0 ? uint8_t(0) : c};
        }
    }
    return true;
}
