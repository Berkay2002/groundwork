#pragma once
#include "Noise.h"
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

    static constexpr int SAND_LEVEL = 21;  // surfaces at/below this are sand
    static constexpr int TREE_CELL = 8;    // one tree candidate per 8x8 cell

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
};
