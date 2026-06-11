#pragma once

#include "sim/Item.h"
#include <array>
#include <cstddef>

namespace crafting {

constexpr int MAX_CRAFT_GRID = 3;
constexpr int MAX_CRAFT_CELLS = MAX_CRAFT_GRID * MAX_CRAFT_GRID;

struct CraftingGrid {
    int width = 2;
    int height = 2;
    std::array<ItemStack, MAX_CRAFT_CELLS> cells{};

    ItemStack& at(int x, int y) { return cells[size_t(y * MAX_CRAFT_GRID + x)]; }
    const ItemStack& at(int x, int y) const {
        return cells[size_t(y * MAX_CRAFT_GRID + x)];
    }
};

struct Recipe {
    const char* name = "";
    int minSurface = 2;
    int patternW = 0;
    int patternH = 0;
    std::array<ItemId, MAX_CRAFT_CELLS> pattern{};
    ItemStack output;
};

size_t recipeCount();
const Recipe& recipeAt(size_t index);
const Recipe* matchRecipe(const CraftingGrid& grid);
ItemStack craftingOutput(const CraftingGrid& grid);
bool craftToCursor(CraftingGrid& grid, ItemStack& cursor);

}
