#include "sim/Crafting.h"

#include <algorithm>

namespace crafting {
namespace {

constexpr std::array<ItemId, MAX_CRAFT_CELLS> pattern(std::initializer_list<ItemId> ids) {
    std::array<ItemId, MAX_CRAFT_CELLS> out{};
    size_t i = 0;
    for (ItemId id : ids) {
        if (i < out.size()) out[i++] = id;
    }
    return out;
}

constexpr Recipe makeRecipe(const char* name, int minSurface, int w, int h,
                            std::array<ItemId, MAX_CRAFT_CELLS> pat,
                            ItemId output, int count) {
    return {name, minSurface, w, h, pat, ItemStack{output, uint8_t(count), 0}};
}

constexpr Recipe makeToolRecipe(const char* name, ItemId material, ToolClass cls,
                                ItemId output) {
    std::array<ItemId, MAX_CRAFT_CELLS> pat{};
    if (cls == ToolClass::Pickaxe) {
        pat = pattern({material, material, material,
                       ItemId::None, ItemId::Stick, ItemId::None,
                       ItemId::None, ItemId::Stick, ItemId::None});
    } else if (cls == ToolClass::Axe) {
        pat = pattern({material, material, ItemId::None,
                       material, ItemId::Stick, ItemId::None,
                       ItemId::None, ItemId::Stick, ItemId::None});
    } else if (cls == ToolClass::Shovel) {
        pat = pattern({ItemId::None, material, ItemId::None,
                       ItemId::None, ItemId::Stick, ItemId::None,
                       ItemId::None, ItemId::Stick, ItemId::None});
    }
    return {name, 3, 3, 3, pat, ItemStack{output, 1, 0}};
}

const std::array<Recipe, 17> RECIPES = {{
    makeRecipe("Planks", 2, 1, 1, pattern({ItemId::LogBlock}), ItemId::PlanksBlock, 4),
    makeRecipe("Sticks", 2, 1, 2, pattern({ItemId::PlanksBlock, ItemId::PlanksBlock}),
               ItemId::Stick, 4),
    makeRecipe("Crafting Table", 2, 2, 2,
               pattern({ItemId::PlanksBlock, ItemId::PlanksBlock,
                        ItemId::PlanksBlock, ItemId::PlanksBlock}),
               ItemId::CraftingTableBlock, 1),
    makeRecipe("Torches", 2, 1, 2, pattern({ItemId::Coal, ItemId::Stick}),
               ItemId::TorchBlock, 4),
    makeRecipe("Furnace", 3, 3, 3,
               pattern({ItemId::CobblestoneBlock, ItemId::CobblestoneBlock, ItemId::CobblestoneBlock,
                        ItemId::CobblestoneBlock, ItemId::None, ItemId::CobblestoneBlock,
                        ItemId::CobblestoneBlock, ItemId::CobblestoneBlock, ItemId::CobblestoneBlock}),
               ItemId::FurnaceBlock, 1),

    makeToolRecipe("Wood Pickaxe", ItemId::PlanksBlock, ToolClass::Pickaxe, ItemId::WoodPickaxe),
    makeToolRecipe("Wood Axe", ItemId::PlanksBlock, ToolClass::Axe, ItemId::WoodAxe),
    makeToolRecipe("Wood Shovel", ItemId::PlanksBlock, ToolClass::Shovel, ItemId::WoodShovel),
    makeToolRecipe("Stone Pickaxe", ItemId::CobblestoneBlock, ToolClass::Pickaxe, ItemId::StonePickaxe),
    makeToolRecipe("Stone Axe", ItemId::CobblestoneBlock, ToolClass::Axe, ItemId::StoneAxe),
    makeToolRecipe("Stone Shovel", ItemId::CobblestoneBlock, ToolClass::Shovel, ItemId::StoneShovel),
    makeToolRecipe("Iron Pickaxe", ItemId::IronIngot, ToolClass::Pickaxe, ItemId::IronPickaxe),
    makeToolRecipe("Iron Axe", ItemId::IronIngot, ToolClass::Axe, ItemId::IronAxe),
    makeToolRecipe("Iron Shovel", ItemId::IronIngot, ToolClass::Shovel, ItemId::IronShovel),
    makeToolRecipe("Diamond Pickaxe", ItemId::Diamond, ToolClass::Pickaxe, ItemId::DiamondPickaxe),
    makeToolRecipe("Diamond Axe", ItemId::Diamond, ToolClass::Axe, ItemId::DiamondAxe),
    makeToolRecipe("Diamond Shovel", ItemId::Diamond, ToolClass::Shovel, ItemId::DiamondShovel),
}};

bool gridCellMatches(const CraftingGrid& grid, int x, int y, ItemId expected) {
    const ItemStack& s = grid.at(x, y);
    return expected == ItemId::None ? s.empty() : (!s.empty() && s.item == expected);
}

bool matchesAt(const CraftingGrid& grid, const Recipe& recipe, int ox, int oy) {
    for (int y = 0; y < grid.height; ++y) {
        for (int x = 0; x < grid.width; ++x) {
            ItemId expected = ItemId::None;
            if (x >= ox && x < ox + recipe.patternW && y >= oy && y < oy + recipe.patternH) {
                int rx = x - ox;
                int ry = y - oy;
                expected = recipe.pattern[size_t(ry * recipe.patternW + rx)];
            }
            if (!gridCellMatches(grid, x, y, expected)) return false;
        }
    }
    return true;
}

ItemStack recipeOutput(const Recipe& recipe) {
    return makeItemStack(recipe.output.item, recipe.output.count, recipe.output.durability);
}

bool cursorCanAccept(const ItemStack& cursor, const ItemStack& output) {
    if (output.empty()) return false;
    if (cursor.empty()) return true;
    if (!stacksCompatible(cursor, output)) return false;
    return int(cursor.count) + int(output.count) <= int(itemDef(cursor.item).stackMax);
}

void consumeRecipe(CraftingGrid& grid, const Recipe& recipe, int ox, int oy) {
    for (int y = 0; y < recipe.patternH; ++y) {
        for (int x = 0; x < recipe.patternW; ++x) {
            ItemId id = recipe.pattern[size_t(y * recipe.patternW + x)];
            if (id == ItemId::None) continue;
            ItemStack& s = grid.at(ox + x, oy + y);
            if (--s.count == 0) s = {};
        }
    }
}

} // namespace

size_t recipeCount() { return RECIPES.size(); }

const Recipe& recipeAt(size_t index) {
    return RECIPES[std::min(index, RECIPES.size() - 1)];
}

const Recipe* matchRecipe(const CraftingGrid& grid) {
    if (grid.width < 1 || grid.width > MAX_CRAFT_GRID ||
        grid.height < 1 || grid.height > MAX_CRAFT_GRID) {
        return nullptr;
    }
    int surface = std::min(grid.width, grid.height);
    for (const Recipe& recipe : RECIPES) {
        if (surface < recipe.minSurface) continue;
        if (recipe.patternW > grid.width || recipe.patternH > grid.height) continue;
        for (int oy = 0; oy <= grid.height - recipe.patternH; ++oy)
            for (int ox = 0; ox <= grid.width - recipe.patternW; ++ox)
                if (matchesAt(grid, recipe, ox, oy)) return &recipe;
    }
    return nullptr;
}

ItemStack craftingOutput(const CraftingGrid& grid) {
    const Recipe* recipe = matchRecipe(grid);
    return recipe ? recipeOutput(*recipe) : ItemStack{};
}

bool craftToCursor(CraftingGrid& grid, ItemStack& cursor) {
    const Recipe* recipe = matchRecipe(grid);
    if (!recipe) return false;
    ItemStack output = recipeOutput(*recipe);
    if (!cursorCanAccept(cursor, output)) return false;

    for (int oy = 0; oy <= grid.height - recipe->patternH; ++oy) {
        for (int ox = 0; ox <= grid.width - recipe->patternW; ++ox) {
            if (!matchesAt(grid, *recipe, ox, oy)) continue;
            consumeRecipe(grid, *recipe, ox, oy);
            if (cursor.empty()) cursor = output;
            else cursor.count = uint8_t(cursor.count + output.count);
            return true;
        }
    }
    return false;
}

}
