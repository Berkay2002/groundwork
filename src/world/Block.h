#pragma once
#include "sim/ItemIds.h"
#include <cstdint>

// Enum values are the bytes written to chunk saves: append, never renumber.
enum class Block : uint8_t {
    Air = 0,
    Grass = 1,
    Dirt = 2,
    Stone = 3,
    Wood = 4,
    Leaves = 5,
    Sand = 6,
    Bedrock = 7,
    Torch = 8,
    CoalOre = 9,
    IronOre = 10,
    Water = 11,
    Cobblestone = 12,
    Planks = 13,
    CraftingTable = 14,
    Furnace = 15,
    DiamondOre = 16,
    FurnaceLit = 17, // burning furnace: same block entity, glows + fire front
};
constexpr int BLOCK_TYPES = 18;

// Texture tile identity. The numeric value is the texture-array layer and the
// column in the HUD's horizontal strip atlas — a renderer/content ID with no
// save-format meaning (unlike Block values, these may be reordered freely).
// Error renders loud magenta so a bad tile mapping is obvious, never
// silently some other block's art.
enum class TileId : uint8_t {
    GrassTop = 0,
    GrassSide,
    Dirt,
    Stone,
    WoodSide,
    WoodTop,
    Leaves,
    Sand,
    Bedrock,
    Torch,
    CoalOre,
    IronOre,
    Water,
    Cobblestone,
    Planks,
    CraftingTableSide,
    CraftingTableTop,
    FurnaceSide,
    FurnaceFront,
    FurnaceFrontLit,
    DiamondOre,
    PlayerArm,
    ItemStick,
    ItemCoal,
    ItemRawIron,
    ItemIronIngot,
    ItemDiamond,
    ItemWoodPickaxe,
    ItemWoodAxe,
    ItemWoodShovel,
    ItemStonePickaxe,
    ItemStoneAxe,
    ItemStoneShovel,
    ItemIronPickaxe,
    ItemIronAxe,
    ItemIronShovel,
    ItemDiamondPickaxe,
    ItemDiamondAxe,
    ItemDiamondShovel,
    Error,
    Count
};
constexpr int ATLAS_TILES = int(TileId::Count);

constexpr float UNBREAKABLE = -1.0f;

// Sound material: which family of break/place recordings a block uses
// (dirt must not clink like stone). None = silent (air, water).
enum class SoundMat : uint8_t { None, Soft, Stone, Wood };

// One row per block id: every per-block property lives here instead of in
// scattered switch statements. Survival mining reads hardness, tool class,
// harvest tier, and item drops directly from this table.
//
// The predicates are deliberately distinct — don't conflate them:
//   solid       raycast target / occupies the cell for placement
//   collidable  blocks player movement (torch and water are walk-through)
//   opaque      stops light AND culls neighbor faces in the mesher
//   dimsSunlight transparent to light but breaks the lossless downward-15
//               sun rule (water: lakes darken ~1 level per block of depth)
struct BlockDef {
    const char* name;
    bool solid;
    bool collidable;
    bool opaque;
    bool dimsSunlight;
    uint8_t emission;  // block light seeded into the cell (0..15)
    float hardness;    // Minecraft-like break hardness; UNBREAKABLE = never breaks
    Block drop;        // legacy block drop, kept until all drop callers use item drops
    SoundMat sound;    // break/place sound family
    ToolClass preferredTool;
    ToolTier minHarvestTier;
    ItemId dropItem;
    uint8_t dropCount;
    ItemId wrongToolDropItem;
    uint8_t wrongToolDropCount;
    TileId tiles[6];   // tile per face: +X -X +Y(top) -Y(bottom) +Z -Z
};

namespace tiledef {
// Row builders so BLOCK_DEFS stays a readable table: most blocks use one tile
// on all six faces; grass/wood differ per face (mesher face order above).
constexpr BlockDef same(const char* name, bool solid, bool collid, bool opaque,
                        bool dimSun, uint8_t em, float hard, Block drop,
                        SoundMat snd, TileId t, ToolClass tool = ToolClass::None,
                        ToolTier tier = ToolTier::Hand,
                        ItemId dropItem = ItemId::None, uint8_t dropCount = 0,
                        ItemId wrongDropItem = ItemId::None,
                        uint8_t wrongDropCount = 0) {
    return {name, solid, collid, opaque, dimSun, em, hard, drop, snd,
            tool, tier, dropItem, dropCount, wrongDropItem, wrongDropCount,
            {t, t, t, t, t, t}};
}
constexpr BlockDef sideTopBot(const char* name, bool solid, bool collid,
                              bool opaque, bool dimSun, uint8_t em,
                              float hard, Block drop, SoundMat snd,
                              TileId side, TileId top, TileId bot,
                              ToolClass tool = ToolClass::None,
                              ToolTier tier = ToolTier::Hand,
                              ItemId dropItem = ItemId::None,
                              uint8_t dropCount = 0,
                              ItemId wrongDropItem = ItemId::None,
                              uint8_t wrongDropCount = 0) {
    return {name, solid, collid, opaque, dimSun, em, hard, drop, snd,
            tool, tier, dropItem, dropCount, wrongDropItem, wrongDropCount,
            {side, side, top, bot, side, side}};
}
}

// Indexed by enum value — rows are append-only, like the enum.
constexpr BlockDef BLOCK_DEFS[BLOCK_TYPES] = {
    //                       name        solid  collid opaque dimSun em  hard  drop            sound            tiles
    /*  0 */ tiledef::same("Air",            false, false, false, false, 0,  0.0f,        Block::Air,         SoundMat::None,  TileId::Error),
    /*  1 */ tiledef::sideTopBot("Grass",    true,  true,  true,  false, 0,  0.6f,        Block::Dirt,        SoundMat::Soft,  TileId::GrassSide, TileId::GrassTop, TileId::Dirt, ToolClass::Shovel, ToolTier::Hand, ItemId::DirtBlock, 1, ItemId::DirtBlock, 1),
    /*  2 */ tiledef::same("Dirt",           true,  true,  true,  false, 0,  0.5f,        Block::Dirt,        SoundMat::Soft,  TileId::Dirt, ToolClass::Shovel, ToolTier::Hand, ItemId::DirtBlock, 1, ItemId::DirtBlock, 1),
    /*  3 */ tiledef::same("Stone",          true,  true,  true,  false, 0,  1.5f,        Block::Cobblestone, SoundMat::Stone, TileId::Stone, ToolClass::Pickaxe, ToolTier::Wood, ItemId::CobblestoneBlock, 1),
    /*  4 */ tiledef::sideTopBot("Log",      true,  true,  true,  false, 0,  2.0f,        Block::Wood,        SoundMat::Wood,  TileId::WoodSide, TileId::WoodTop, TileId::WoodTop, ToolClass::Axe, ToolTier::Hand, ItemId::LogBlock, 1, ItemId::LogBlock, 1),
    /*  5 */ tiledef::same("Leaves",         true,  true,  true,  false, 0,  0.2f,        Block::Air,         SoundMat::Soft,  TileId::Leaves),
    /*  6 */ tiledef::same("Sand",           true,  true,  true,  false, 0,  0.5f,        Block::Sand,        SoundMat::Soft,  TileId::Sand, ToolClass::Shovel, ToolTier::Hand, ItemId::SandBlock, 1, ItemId::SandBlock, 1),
    /*  7 */ tiledef::same("Bedrock",        true,  true,  true,  false, 0,  UNBREAKABLE, Block::Air,         SoundMat::Stone, TileId::Bedrock),
    /*  8 */ tiledef::same("Torch",          true,  false, false, false, 14, 0.0f,        Block::Torch,       SoundMat::Wood,  TileId::Torch, ToolClass::None, ToolTier::Hand, ItemId::TorchBlock, 1, ItemId::TorchBlock, 1),
    /*  9 */ tiledef::same("Coal Ore",       true,  true,  true,  false, 0,  3.0f,        Block::Air,         SoundMat::Stone, TileId::CoalOre, ToolClass::Pickaxe, ToolTier::Wood, ItemId::Coal, 1),
    /* 10 */ tiledef::same("Iron Ore",       true,  true,  true,  false, 0,  3.0f,        Block::Air,         SoundMat::Stone, TileId::IronOre, ToolClass::Pickaxe, ToolTier::Stone, ItemId::RawIron, 1),
    /* 11 */ tiledef::same("Water",          false, false, false, true,  0,  0.0f,        Block::Air,         SoundMat::None,  TileId::Water),
    /* 12 */ tiledef::same("Cobblestone",    true,  true,  true,  false, 0,  2.0f,        Block::Cobblestone, SoundMat::Stone, TileId::Cobblestone, ToolClass::Pickaxe, ToolTier::Wood, ItemId::CobblestoneBlock, 1),
    /* 13 */ tiledef::same("Planks",         true,  true,  true,  false, 0,  2.0f,        Block::Planks,      SoundMat::Wood,  TileId::Planks, ToolClass::Axe, ToolTier::Hand, ItemId::PlanksBlock, 1, ItemId::PlanksBlock, 1),
    /* 14 */ tiledef::sideTopBot("Crafting Table", true, true, true, false, 0, 2.5f,      Block::CraftingTable, SoundMat::Wood, TileId::CraftingTableSide, TileId::CraftingTableTop, TileId::Planks, ToolClass::Axe, ToolTier::Hand, ItemId::CraftingTableBlock, 1, ItemId::CraftingTableBlock, 1),
    // The furnace has no facing metadata, so the front (mouth) shows on all
    // four side faces; top/bottom use the plain side tile.
    /* 15 */ tiledef::sideTopBot("Furnace",  true,  true,  true,  false, 0,  3.5f,        Block::Air,         SoundMat::Stone, TileId::FurnaceFront, TileId::FurnaceSide, TileId::FurnaceSide, ToolClass::Pickaxe, ToolTier::Wood, ItemId::FurnaceBlock, 1),
    /* 16 */ tiledef::same("Diamond Ore",    true,  true,  true,  false, 0,  3.0f,        Block::Air,         SoundMat::Stone, TileId::DiamondOre, ToolClass::Pickaxe, ToolTier::Iron, ItemId::Diamond, 1),
    /* 17 */ tiledef::sideTopBot("Furnace",  true,  true,  true,  false, 13, 3.5f,        Block::Air,         SoundMat::Stone, TileId::FurnaceFrontLit, TileId::FurnaceSide, TileId::FurnaceSide, ToolClass::Pickaxe, ToolTier::Wood, ItemId::FurnaceBlock, 1),
};

// Furnace lit/unlit are one logical block (shared block entity, same drops);
// gameplay code that targets "a furnace" must accept both.
inline bool isFurnaceBlock(Block b) {
    return b == Block::Furnace || b == Block::FurnaceLit;
}

inline const BlockDef& blockDef(Block b) { return BLOCK_DEFS[uint8_t(b)]; }

inline const char* blockName(Block b) { return blockDef(b).name; }
inline bool isSolid(Block b) { return blockDef(b).solid; }
inline bool isBreakable(Block b) {
    return blockDef(b).solid && blockDef(b).hardness != UNBREAKABLE;
}
inline bool isCollidable(Block b) { return blockDef(b).collidable; }
inline bool isOpaque(Block b) { return blockDef(b).opaque; }
inline bool dimsSunlight(Block b) { return blockDef(b).dimsSunlight; }
inline uint8_t lightEmission(Block b) { return blockDef(b).emission; }
inline SoundMat soundMaterial(Block b) { return blockDef(b).sound; }

// Face order used by the mesher: 0 +X, 1 -X, 2 +Y(top), 3 -Y(bottom), 4 +Z, 5 -Z
// Returns the numeric tile (texture-array layer / atlas column) for rendering.
inline int tileFor(Block b, int face) { return int(blockDef(b).tiles[face]); }
