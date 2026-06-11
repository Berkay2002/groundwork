#pragma once
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
};
constexpr int BLOCK_TYPES = 12;

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
    Error,
    Count
};
constexpr int ATLAS_TILES = int(TileId::Count);

constexpr uint8_t UNBREAKABLE = 0xFF;

// Sound material: which family of break/place recordings a block uses
// (dirt must not clink like stone). None = silent (air, water).
enum class SoundMat : uint8_t { None, Soft, Stone, Wood };

// One row per block id: every per-block property lives here instead of in
// scattered switch statements. Hardness (relative break time) and drop are
// recorded now for Batch G (finite blocks, item drops) but unused until then.
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
    uint8_t hardness;  // relative break time; UNBREAKABLE = never breaks
    Block drop;        // what breaking yields (Batch G)
    SoundMat sound;    // break/place sound family
    TileId tiles[6];   // tile per face: +X -X +Y(top) -Y(bottom) +Z -Z
};

namespace tiledef {
// Row builders so BLOCK_DEFS stays a readable table: most blocks use one tile
// on all six faces; grass/wood differ per face (mesher face order above).
constexpr BlockDef same(const char* name, bool solid, bool collid, bool opaque,
                        bool dimSun, uint8_t em, uint8_t hard, Block drop,
                        SoundMat snd, TileId t) {
    return {name, solid, collid, opaque, dimSun, em, hard, drop, snd,
            {t, t, t, t, t, t}};
}
constexpr BlockDef sideTopBot(const char* name, bool solid, bool collid,
                              bool opaque, bool dimSun, uint8_t em,
                              uint8_t hard, Block drop, SoundMat snd,
                              TileId side, TileId top, TileId bot) {
    return {name, solid, collid, opaque, dimSun, em, hard, drop, snd,
            {side, side, top, bot, side, side}};
}
}

// Indexed by enum value — rows are append-only, like the enum.
constexpr BlockDef BLOCK_DEFS[BLOCK_TYPES] = {
    //                       name        solid  collid opaque dimSun em  hard  drop            sound            tiles
    /*  0 */ tiledef::same("Air",         false, false, false, false, 0,  0,    Block::Air,     SoundMat::None,  TileId::Error),
    /*  1 */ tiledef::sideTopBot("Grass", true, true,  true,  false, 0,  2,    Block::Dirt,    SoundMat::Soft,  TileId::GrassSide, TileId::GrassTop, TileId::Dirt),
    /*  2 */ tiledef::same("Dirt",        true,  true,  true,  false, 0,  2,    Block::Dirt,    SoundMat::Soft,  TileId::Dirt),
    /*  3 */ tiledef::same("Stone",       true,  true,  true,  false, 0,  6,    Block::Stone,   SoundMat::Stone, TileId::Stone),
    /*  4 */ tiledef::sideTopBot("Wood",  true,  true,  true,  false, 0,  3,    Block::Wood,    SoundMat::Wood,  TileId::WoodSide, TileId::WoodTop, TileId::WoodTop),
    /*  5 */ tiledef::same("Leaves",      true,  true,  true,  false, 0,  1,    Block::Air,     SoundMat::Soft,  TileId::Leaves),
    /*  6 */ tiledef::same("Sand",        true,  true,  true,  false, 0,  2,    Block::Sand,    SoundMat::Soft,  TileId::Sand),
    /*  7 */ tiledef::same("Bedrock",     true,  true,  true,  false, 0,  UNBREAKABLE, Block::Air, SoundMat::Stone, TileId::Bedrock),
    /*  8 */ tiledef::same("Torch",       true,  false, false, false, 14, 1,    Block::Torch,   SoundMat::Wood,  TileId::Torch),
    /*  9 */ tiledef::same("Coal Ore",    true,  true,  true,  false, 0,  6,    Block::CoalOre, SoundMat::Stone, TileId::CoalOre),
    /* 10 */ tiledef::same("Iron Ore",    true,  true,  true,  false, 0,  8,    Block::IronOre, SoundMat::Stone, TileId::IronOre),
    /* 11 */ tiledef::same("Water",       false, false, false, true,  0,  0,    Block::Air,     SoundMat::None,  TileId::Water),
};

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
