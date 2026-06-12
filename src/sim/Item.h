#pragma once

#include "sim/ItemIds.h"
#include "world/Block.h"
#include <algorithm>
#include <cstdint>

struct ItemDef {
    const char* name;
    uint8_t stackMax;
    Block placeBlock;
    ToolClass toolClass;
    ToolTier toolTier;
    uint16_t maxDurability;
    float miningSpeed;
    int fuelTicks;
};

namespace itemdef {
constexpr ItemDef simple(const char* name, uint8_t stackMax = 64,
                         int fuelTicks = 0) {
    return {name, stackMax, Block::Air, ToolClass::None, ToolTier::Hand, 0,
            1.0f, fuelTicks};
}

constexpr ItemDef block(const char* name, Block b) {
    return {name, 64, b, ToolClass::None, ToolTier::Hand, 0, 1.0f, 0};
}

constexpr ItemDef tool(const char* name, ToolClass cls, ToolTier tier,
                       uint16_t durability, float speed) {
    return {name, 1, Block::Air, cls, tier, durability, speed, 0};
}
}

constexpr ItemDef ITEM_DEFS[ITEM_TYPES] = {
    /*  0 */ itemdef::simple("None", 1),
    /*  1 */ itemdef::block("Grass", Block::Grass),
    /*  2 */ itemdef::block("Dirt", Block::Dirt),
    /*  3 */ itemdef::block("Stone", Block::Stone),
    /*  4 */ itemdef::block("Log", Block::Wood),
    /*  5 */ itemdef::block("Leaves", Block::Leaves),
    /*  6 */ itemdef::block("Sand", Block::Sand),
    /*  7 */ itemdef::block("Bedrock", Block::Bedrock),
    /*  8 */ itemdef::block("Torch", Block::Torch),
    /*  9 */ itemdef::block("Coal Ore", Block::CoalOre),
    /* 10 */ itemdef::block("Iron Ore", Block::IronOre),
    /* 11 */ itemdef::block("Water", Block::Water),
    /* 12 */ itemdef::simple("Stick"),
    /* 13 */ itemdef::simple("Coal", 64, 1600),
    /* 14 */ itemdef::simple("Raw Iron"),
    /* 15 */ itemdef::simple("Iron Ingot"),
    /* 16 */ itemdef::simple("Diamond"),
    /* 17 */ itemdef::tool("Wooden Pickaxe", ToolClass::Pickaxe, ToolTier::Wood, 59, 2.0f),
    /* 18 */ itemdef::tool("Wooden Axe", ToolClass::Axe, ToolTier::Wood, 59, 2.0f),
    /* 19 */ itemdef::tool("Wooden Shovel", ToolClass::Shovel, ToolTier::Wood, 59, 2.0f),
    /* 20 */ itemdef::tool("Stone Pickaxe", ToolClass::Pickaxe, ToolTier::Stone, 131, 4.0f),
    /* 21 */ itemdef::tool("Stone Axe", ToolClass::Axe, ToolTier::Stone, 131, 4.0f),
    /* 22 */ itemdef::tool("Stone Shovel", ToolClass::Shovel, ToolTier::Stone, 131, 4.0f),
    /* 23 */ itemdef::tool("Iron Pickaxe", ToolClass::Pickaxe, ToolTier::Iron, 250, 6.0f),
    /* 24 */ itemdef::tool("Iron Axe", ToolClass::Axe, ToolTier::Iron, 250, 6.0f),
    /* 25 */ itemdef::tool("Iron Shovel", ToolClass::Shovel, ToolTier::Iron, 250, 6.0f),
    /* 26 */ itemdef::tool("Diamond Pickaxe", ToolClass::Pickaxe, ToolTier::Diamond, 1561, 8.0f),
    /* 27 */ itemdef::tool("Diamond Axe", ToolClass::Axe, ToolTier::Diamond, 1561, 8.0f),
    /* 28 */ itemdef::tool("Diamond Shovel", ToolClass::Shovel, ToolTier::Diamond, 1561, 8.0f),
    /* 29 */ itemdef::block("Cobblestone", Block::Cobblestone),
    /* 30 */ itemdef::block("Planks", Block::Planks),
    /* 31 */ itemdef::block("Crafting Table", Block::CraftingTable),
    /* 32 */ itemdef::block("Furnace", Block::Furnace),
    /* 33 */ itemdef::block("Diamond Ore", Block::DiamondOre),
};

inline const ItemDef& itemDef(ItemId id) {
    uint16_t i = uint16_t(id);
    return ITEM_DEFS[i < ITEM_TYPES ? i : 0];
}

inline bool isValidItemId(ItemId id) {
    uint16_t i = uint16_t(id);
    return i < ITEM_TYPES && id != ItemId::None;
}

inline Block placeBlockForItem(ItemId id) {
    return itemDef(id).placeBlock;
}

inline ItemId itemForBlock(Block b) {
    switch (b) {
        case Block::Air: return ItemId::None;
        case Block::Grass: return ItemId::GrassBlock;
        case Block::Dirt: return ItemId::DirtBlock;
        case Block::Stone: return ItemId::StoneBlock;
        case Block::Wood: return ItemId::LogBlock;
        case Block::Leaves: return ItemId::LeavesBlock;
        case Block::Sand: return ItemId::SandBlock;
        case Block::Bedrock: return ItemId::BedrockBlock;
        case Block::Torch: return ItemId::TorchBlock;
        case Block::CoalOre: return ItemId::CoalOreBlock;
        case Block::IronOre: return ItemId::IronOreBlock;
        case Block::Water: return ItemId::WaterBlock;
        case Block::Cobblestone: return ItemId::CobblestoneBlock;
        case Block::Planks: return ItemId::PlanksBlock;
        case Block::CraftingTable: return ItemId::CraftingTableBlock;
        case Block::Furnace: return ItemId::FurnaceBlock;
        case Block::FurnaceLit: return ItemId::FurnaceBlock;
        case Block::DiamondOre: return ItemId::DiamondOreBlock;
    }
    return ItemId::None;
}

struct ItemStack {
    ItemId item = ItemId::None;
    uint8_t count = 0;
    uint16_t durability = 0;
    bool empty() const { return count == 0 || item == ItemId::None; }
};

inline ItemStack makeItemStack(ItemId id, int count, uint16_t durability = 0) {
    if (!isValidItemId(id) || count <= 0) return {};
    const ItemDef& d = itemDef(id);
    if (d.maxDurability > 0) {
        uint16_t dur = durability == 0 ? d.maxDurability
                                       : std::min(durability, d.maxDurability);
        return {id, 1, dur};
    }
    return {id, uint8_t(std::min(count, int(d.stackMax))), 0};
}

inline ItemStack sanitizeLoadedItemStack(uint16_t rawId, uint8_t count,
                                         uint16_t durability) {
    if (rawId >= ITEM_TYPES || rawId == 0 || count == 0) return {};
    ItemId id = ItemId(rawId);
    const ItemDef& d = itemDef(id);
    if (d.maxDurability > 0 && durability == 0) return {};
    return makeItemStack(id, count, durability);
}

inline ItemStack normalizeItemStack(ItemStack s) {
    return makeItemStack(s.item, s.count, s.durability);
}

inline ItemStack makeToolStack(ItemId id) {
    return makeItemStack(id, 1);
}

inline bool stacksCompatible(const ItemStack& a, const ItemStack& b) {
    if (a.empty() || b.empty()) return false;
    const ItemDef& d = itemDef(a.item);
    if (a.item != b.item) return false;
    if (d.maxDurability > 0) return a.durability == b.durability;
    return d.stackMax > 1;
}
