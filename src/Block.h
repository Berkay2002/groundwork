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

// Texture atlas tile indices (atlas is a horizontal strip of 16x16 tiles).
// 0 grass top, 1 grass side, 2 dirt, 3 stone, 4 wood side, 5 wood top,
// 6 leaves, 7 sand, 8 bedrock, 9 torch, 10 coal ore, 11 iron ore, 12 water
constexpr int ATLAS_TILES = 13;

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
    uint8_t tiles[6];  // atlas tile per face: +X -X +Y(top) -Y(bottom) +Z -Z
};

// Indexed by enum value — rows are append-only, like the enum.
constexpr BlockDef BLOCK_DEFS[BLOCK_TYPES] = {
    //          name        solid  collid opaque dimSun em  hard  drop            sound            +X  -X  +Y  -Y  +Z  -Z
    /*  0 */ {"Air",        false, false, false, false, 0,  0,    Block::Air,     SoundMat::None,  { 0,  0,  0,  0,  0,  0}},
    /*  1 */ {"Grass",      true,  true,  true,  false, 0,  2,    Block::Dirt,    SoundMat::Soft,  { 1,  1,  0,  2,  1,  1}},
    /*  2 */ {"Dirt",       true,  true,  true,  false, 0,  2,    Block::Dirt,    SoundMat::Soft,  { 2,  2,  2,  2,  2,  2}},
    /*  3 */ {"Stone",      true,  true,  true,  false, 0,  6,    Block::Stone,   SoundMat::Stone, { 3,  3,  3,  3,  3,  3}},
    /*  4 */ {"Wood",       true,  true,  true,  false, 0,  3,    Block::Wood,    SoundMat::Wood,  { 4,  4,  5,  5,  4,  4}},
    /*  5 */ {"Leaves",     true,  true,  true,  false, 0,  1,    Block::Air,     SoundMat::Soft,  { 6,  6,  6,  6,  6,  6}},
    /*  6 */ {"Sand",       true,  true,  true,  false, 0,  2,    Block::Sand,    SoundMat::Soft,  { 7,  7,  7,  7,  7,  7}},
    /*  7 */ {"Bedrock",    true,  true,  true,  false, 0,  UNBREAKABLE,
                                                            Block::Air,           SoundMat::Stone, { 8,  8,  8,  8,  8,  8}},
    /*  8 */ {"Torch",      true,  false, false, false, 14, 1,    Block::Torch,   SoundMat::Wood,  { 9,  9,  9,  9,  9,  9}},
    /*  9 */ {"Coal Ore",   true,  true,  true,  false, 0,  6,    Block::CoalOre, SoundMat::Stone, {10, 10, 10, 10, 10, 10}},
    /* 10 */ {"Iron Ore",   true,  true,  true,  false, 0,  8,    Block::IronOre, SoundMat::Stone, {11, 11, 11, 11, 11, 11}},
    /* 11 */ {"Water",      false, false, false, true,  0,  0,    Block::Air,     SoundMat::None,  {12, 12, 12, 12, 12, 12}},
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
inline int tileFor(Block b, int face) { return blockDef(b).tiles[face]; }
