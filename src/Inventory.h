#pragma once
#include "Block.h"
#include <algorithm>

struct ItemStack {
    Block block = Block::Air;
    uint8_t count = 0;
    bool empty() const { return count == 0 || block == Block::Air; }
};

// 4 rows x 8 columns of stacks; row 0 (slots 0..7) is the hotbar, matching
// the 8 hotbar keys. Pure logic, GL-free, saved in player.bin v2.
class Inventory {
public:
    static constexpr int COLS = 8, ROWS = 4, SLOTS = COLS * ROWS;
    static constexpr int STACK_MAX = 64;

    ItemStack slots[SLOTS]; // slot 0..7 = hotbar, then the grid rows

    // Add n of b: top up existing stacks first (hotbar first), then fill
    // empty slots. Returns how many didn't fit.
    int add(Block b, int n) {
        if (b == Block::Air || n <= 0) return n < 0 ? 0 : n;
        for (int i = 0; i < SLOTS && n > 0; ++i) {
            ItemStack& s = slots[i];
            if (!s.empty() && s.block == b && s.count < STACK_MAX) {
                int take = std::min(n, STACK_MAX - int(s.count));
                s.count = uint8_t(s.count + take);
                n -= take;
            }
        }
        for (int i = 0; i < SLOTS && n > 0; ++i) {
            ItemStack& s = slots[i];
            if (s.empty()) {
                int take = std::min(n, STACK_MAX);
                s = {b, uint8_t(take)};
                n -= take;
            }
        }
        return n;
    }

    // Remove one item (a placement). False if the slot is empty/invalid.
    bool consumeOne(int slot) {
        if (slot < 0 || slot >= SLOTS) return false;
        ItemStack& s = slots[slot];
        if (s.empty()) return false;
        if (--s.count == 0) s.block = Block::Air;
        return true;
    }
};
