#pragma once

#include <cstdint>

enum class ToolClass : uint8_t { None, Pickaxe, Axe, Shovel };
enum class ToolTier : uint8_t { Hand, Wood, Stone, Iron, Diamond };

inline int tierLevel(ToolTier t) {
    switch (t) {
        case ToolTier::Hand: return 0;
        case ToolTier::Wood: return 1;
        case ToolTier::Stone: return 2;
        case ToolTier::Iron: return 3;
        case ToolTier::Diamond: return 4;
    }
    return 0;
}

// Item ids are saved in player/block-entity files: append only, never
// renumber. Block ids remain separate terrain save bytes.
enum class ItemId : uint16_t {
    None = 0,
    GrassBlock = 1,
    DirtBlock = 2,
    StoneBlock = 3,
    LogBlock = 4,
    LeavesBlock = 5,
    SandBlock = 6,
    BedrockBlock = 7,
    TorchBlock = 8,
    CoalOreBlock = 9,
    IronOreBlock = 10,
    WaterBlock = 11,
    Stick = 12,
    Coal = 13,
    RawIron = 14,
    IronIngot = 15,
    Diamond = 16,
    WoodPickaxe = 17,
    WoodAxe = 18,
    WoodShovel = 19,
    StonePickaxe = 20,
    StoneAxe = 21,
    StoneShovel = 22,
    IronPickaxe = 23,
    IronAxe = 24,
    IronShovel = 25,
    DiamondPickaxe = 26,
    DiamondAxe = 27,
    DiamondShovel = 28,
    CobblestoneBlock = 29,
    PlanksBlock = 30,
    CraftingTableBlock = 31,
    FurnaceBlock = 32,
    DiamondOreBlock = 33,
    RottenFlesh = 34,
    Count
};

constexpr int ITEM_TYPES = int(ItemId::Count);
