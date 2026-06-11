#pragma once
#include "world/Noise.h"
#include <cstdint>

class Chunk;

// Deterministic terrain: everything is a pure function of world coordinates
// and the seed, so chunks can be generated independently in any order and
// trees overlapping a chunk border come out identical on both sides.
class Terrain {
public:
    explicit Terrain(uint32_t seed);

    void generateChunk(Chunk& c) const;

    int heightAt(int wx, int wz) const; // y of the surface block

    // True if the cave noise removes this block. `height` is the column's
    // heightAt (passed in so generation doesn't recompute it per block).
    // Carving may breach the surface (cave mouths) but never bedrock, and
    // keeps a floor under water columns so lakes don't drain into caves.
    bool isCarved(int wx, int wy, int wz, int height) const;

    static constexpr int SAND_LEVEL = 21;  // surfaces at/below this are sand
    static constexpr int SEA_LEVEL = 20;   // air at/below this becomes water
    static constexpr int TREE_CELL = 8;    // one tree candidate per 8x8 cell
    static constexpr int ORE_CELL = 8;     // one vein candidate per ore per 8^3 cell
    static constexpr int COAL_MAX_Y = 44;  // depth bands (vein centers)
    static constexpr int IRON_MAX_Y = 22;
    static constexpr int DIAMOND_MAX_Y = 14;

    struct Tree {
        bool exists = false;
        int x = 0, z = 0;       // trunk base column (world coords)
        int baseY = 0;          // ground height at trunk
        int trunkH = 0;
    };
    // The candidate tree of the cell containing (cellX*8, cellZ*8).
    Tree treeInCell(int cellX, int cellZ) const;

private:
    uint32_t seed_;
    Noise plains_;
    Noise hills_;
    Noise hillMask_;
    Noise caveA_;
    Noise caveB_;
    Noise basin_;
};
