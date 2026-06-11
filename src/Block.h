#pragma once
#include <cstdint>

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
};

// Solid: targetable by the raycast / counts as occupied for placement.
inline bool isSolid(Block b) { return b != Block::Air; }
inline bool isBreakable(Block b) { return b != Block::Air && b != Block::Bedrock; }
// Collidable: blocks player movement. Torches are walk-through.
inline bool isCollidable(Block b) { return b != Block::Air && b != Block::Torch; }

// Light model: opaque blocks stop light; emitters seed block light (0..15).
// Torches don't fill their cell, so light (incl. sunlight) passes through.
inline bool isOpaque(Block b) { return b != Block::Air && b != Block::Torch; }
inline uint8_t lightEmission(Block b) { return b == Block::Torch ? 14 : 0; }

// Texture atlas tile indices (atlas is a horizontal strip of 16x16 tiles).
// 0 grass top, 1 grass side, 2 dirt, 3 stone, 4 wood side, 5 wood top,
// 6 leaves, 7 sand, 8 bedrock, 9 torch
constexpr int ATLAS_TILES = 10;

// Face order used by the mesher: 0 +X, 1 -X, 2 +Y(top), 3 -Y(bottom), 4 +Z, 5 -Z
inline int tileFor(Block b, int face) {
    switch (b) {
        case Block::Grass:
            if (face == 2) return 0;      // top
            if (face == 3) return 2;      // bottom = dirt
            return 1;                     // sides
        case Block::Dirt:    return 2;
        case Block::Stone:   return 3;
        case Block::Wood:
            return (face == 2 || face == 3) ? 5 : 4;
        case Block::Leaves:  return 6;
        case Block::Sand:    return 7;
        case Block::Bedrock: return 8;
        case Block::Torch:   return 9;
        default:             return 2;
    }
}
