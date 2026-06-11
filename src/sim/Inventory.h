#pragma once
#include "sim/Item.h"
#include <algorithm>

// 4 rows x 8 columns of stacks; row 0 (slots 0..7) is the hotbar, matching
// the 8 hotbar keys. Pure logic, GL-free, saved in player.bin.
class Inventory {
public:
    static constexpr int COLS = 8, ROWS = 4, SLOTS = COLS * ROWS;
    static constexpr int STACK_MAX = 64;

    ItemStack slots[SLOTS]; // slot 0..7 = hotbar, then the grid rows

    // Add n of an item: top up existing stacks first (hotbar first), then fill
    // empty slots. Returns how many didn't fit. Durable items occupy one slot
    // each and are created at full durability.
    int add(ItemId item, int n) {
        if (!isValidItemId(item) || n <= 0) return n < 0 ? 0 : n;
        const ItemDef& d = itemDef(item);
        ItemStack incoming = makeItemStack(item, 1);
        for (int i = 0; i < SLOTS && n > 0; ++i) {
            ItemStack& s = slots[i];
            if (!s.empty() && stacksCompatible(s, incoming) && s.count < d.stackMax) {
                int take = std::min(n, int(d.stackMax) - int(s.count));
                s.count = uint8_t(s.count + take);
                n -= take;
            }
        }
        for (int i = 0; i < SLOTS && n > 0; ++i) {
            ItemStack& s = slots[i];
            if (s.empty()) {
                int take = std::min(n, int(d.stackMax));
                s = makeItemStack(item, take);
                n -= take;
            }
        }
        return n;
    }

    int add(Block b, int n) { return add(itemForBlock(b), n); }

    int addStack(ItemStack stack) {
        int n = stack.count;
        stack = makeItemStack(stack.item, 1, stack.durability);
        if (stack.empty()) return n;
        const ItemDef& d = itemDef(stack.item);
        for (int i = 0; i < SLOTS && n > 0; ++i) {
            ItemStack& s = slots[i];
            if (!s.empty() && stacksCompatible(s, stack) && s.count < d.stackMax) {
                int take = std::min(n, int(d.stackMax) - int(s.count));
                s.count = uint8_t(s.count + take);
                n -= take;
            }
        }
        for (int i = 0; i < SLOTS && n > 0; ++i) {
            ItemStack& s = slots[i];
            if (s.empty()) {
                int take = std::min(n, int(d.stackMax));
                s = {stack.item, uint8_t(take), stack.durability};
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
        if (--s.count == 0) s = {};
        return true;
    }
};
