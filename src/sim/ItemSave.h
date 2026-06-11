#pragma once

#include "sim/Item.h"
#include <istream>
#include <ostream>

inline void writeItemStack(std::ostream& out, ItemStack stack) {
    stack = normalizeItemStack(stack);
    uint16_t item = uint16_t(stack.item);
    uint8_t count = stack.count;
    uint16_t durability = stack.durability;
    out.write(reinterpret_cast<const char*>(&item), 2);
    out.write(reinterpret_cast<const char*>(&count), 1);
    out.write(reinterpret_cast<const char*>(&durability), 2);
}

inline bool readItemStack(std::istream& in, ItemStack& out) {
    uint16_t item = 0, durability = 0;
    uint8_t count = 0;
    in.read(reinterpret_cast<char*>(&item), 2);
    in.read(reinterpret_cast<char*>(&count), 1);
    in.read(reinterpret_cast<char*>(&durability), 2);
    if (!in) return false;
    out = sanitizeLoadedItemStack(item, count, durability);
    return true;
}
