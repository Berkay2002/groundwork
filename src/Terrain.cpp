#include "Terrain.h"
#include "Chunk.h"
#include <algorithm>
#include <cmath>

namespace {
uint32_t hash2(int32_t x, int32_t z, uint32_t seed) {
    uint32_t h = seed;
    h ^= uint32_t(x) * 0x85EBCA6Bu;
    h = (h << 13) | (h >> 19);
    h ^= uint32_t(z) * 0xC2B2AE35u;
    h *= 0x27D4EB2Fu;
    h ^= h >> 15;
    return h;
}
float smoothstep01(float e0, float e1, float v) {
    float t = std::clamp((v - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
int floorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
}

Terrain::Terrain(uint32_t seed)
    : seed_(seed),
      plains_(seed),
      hills_(seed ^ 0x51AB3E47u),
      hillMask_(seed ^ 0xC0FFEE11u),
      caveA_(seed ^ 0xCA7E0001u),
      caveB_(seed ^ 0x5EEDCAFEu),
      basin_(seed ^ 0x5A17BEEFu) {}

int Terrain::heightAt(int wx, int wz) const {
    float x = float(wx), z = float(wz);
    // Gentle rolling plains everywhere.
    float plains = plains_.fbm(x, z, 4, 1.0f / 64.0f) * 6.0f;
    // Occasional hill regions selected by a very low-frequency mask.
    float mask = smoothstep01(0.05f, 0.45f, hillMask_.fbm(x, z, 2, 1.0f / 256.0f));
    float hills = (hills_.fbm(x, z, 5, 1.0f / 96.0f) * 0.5f + 0.5f) * 32.0f * mask;
    // Lake basins: a low-frequency mask depresses terrain below SEA_LEVEL in
    // spots, giving lakes a few chunks wide with sandy shores (the basin
    // floor lands at/under SAND_LEVEL).
    float basin = smoothstep01(0.42f, 0.70f, basin_.fbm(x, z, 2, 1.0f / 192.0f)) * 12.0f;
    int h = 24 + (int)std::floor(plains + hills - basin);
    return std::clamp(h, 1, CHUNK_HEIGHT - 8);
}

// "Spaghetti" caves: tunnels form where two independent 3D noise fields are
// both near zero (the intersection of two implicit surfaces). Pure function
// of world coordinates + seed, like all generation.
bool Terrain::isCarved(int wx, int wy, int wz, int height) const {
    if (wy < 1 || wy > height) return false;       // bedrock stays; air is air
    // Keep a solid floor under (future) water columns and shoreline so lakes
    // can't open into a cave below.
    if (height < SEA_LEVEL + 2 && wy > height - 4) return false;
    float x = float(wx), y = float(wy), z = float(wz);
    // Tunnels pinch closed near the surface so cave mouths stay occasional.
    float depth = float(height - wy);
    float T = 0.085f * std::clamp(depth / 12.0f, 0.3f, 1.0f);
    if (std::fabs(caveA_.fbm3(x, y * 1.6f, z, 2, 1.0f / 48.0f)) >= T) return false;
    return std::fabs(caveB_.fbm3(x, y * 1.6f, z, 2, 1.0f / 48.0f)) < T;
}

Terrain::Tree Terrain::treeInCell(int cellX, int cellZ) const {
    Tree t;
    uint32_t h = hash2(cellX, cellZ, seed_ ^ 0x7EE51234u);
    if ((h & 0xFF) > 96) return t; // ~38% of cells get a tree candidate
    // Keep the trunk away from cell edges so leaf radius stays within one cell
    // margin (2 blocks) of neighboring chunks.
    t.x = cellX * TREE_CELL + 2 + int((h >> 8) % (TREE_CELL - 4));
    t.z = cellZ * TREE_CELL + 2 + int((h >> 16) % (TREE_CELL - 4));
    t.baseY = heightAt(t.x, t.z) + 1; // first trunk block sits on the surface
    // No trees on sand or high slopes/mountain tops.
    if (t.baseY - 1 <= SAND_LEVEL || t.baseY > 52) return t;
    // No floating trees over cave mouths.
    if (isCarved(t.x, t.baseY - 1, t.z, t.baseY - 1)) return t;
    t.trunkH = 4 + int((h >> 24) % 3); // 4..6
    t.exists = true;
    return t;
}

void Terrain::generateChunk(Chunk& c) const {
    const int bx = c.cx() * CHUNK_SIZE;
    const int bz = c.cz() * CHUNK_SIZE;

    // --- Ground ---
    int heights[CHUNK_SIZE][CHUNK_SIZE];
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            int height = heights[z][x] = heightAt(bx + x, bz + z);
            bool sandy = height <= SAND_LEVEL;
            for (int y = 0; y <= height; ++y) {
                Block b;
                if (y == 0) b = Block::Bedrock;
                else if (y == height) b = sandy ? Block::Sand : Block::Grass;
                else if (y >= height - 3) b = sandy ? Block::Sand : Block::Dirt;
                else b = Block::Stone;
                c.set(x, y, z, b);
            }
        }
    }

    // --- Caves ---
    for (int z = 0; z < CHUNK_SIZE; ++z)
        for (int x = 0; x < CHUNK_SIZE; ++x)
            for (int y = 1; y <= heights[z][x]; ++y)
                if (isCarved(bx + x, y, bz + z, heights[z][x]))
                    c.set(x, y, z, Block::Air);

    // --- Water ---
    // Static fill from the basin floor up to SEA_LEVEL. Runs after carving
    // (the cells above ground are untouched by caves anyway; the lakebed
    // floor is protected inside isCarved).
    for (int z = 0; z < CHUNK_SIZE; ++z)
        for (int x = 0; x < CHUNK_SIZE; ++x)
            for (int y = heights[z][x] + 1; y <= SEA_LEVEL; ++y)
                c.set(x, y, z, Block::Water);

    // --- Ores ---
    // One vein candidate per ore type per 8^3 cell, hashed from the cell
    // coordinates (pure function of world position + seed). The vein is a
    // clumpy blob in a 3x3x3 box around a center kept >=2 from the cell's
    // xz edges, so a vein never leaves its cell; since 8 divides CHUNK_SIZE,
    // veins also never cross chunk borders. Ore only replaces stone, so
    // caves carved beforehand stay open.
    struct Ore { Block b; uint32_t salt; int maxY; int chance; };
    static const Ore ORES[] = {
        {Block::CoalOre, 0x0C0A1000u, COAL_MAX_Y, 110}, // ~43% of cells in band
        {Block::IronOre, 0x10F09000u, IRON_MAX_Y, 80},  // ~31%
    };
    for (const Ore& ore : ORES) {
        for (int cy = 0; cy * ORE_CELL <= ore.maxY; ++cy) {
            for (int cz = bz / ORE_CELL; cz < (bz + CHUNK_SIZE) / ORE_CELL; ++cz) {
                for (int cx = bx / ORE_CELL; cx < (bx + CHUNK_SIZE) / ORE_CELL; ++cx) {
                    uint32_t h = hash2(cx * 1024 + cy, cz, seed_ ^ ore.salt);
                    if ((h & 0xFF) >= uint32_t(ore.chance)) continue;
                    int vx = cx * ORE_CELL + 2 + int((h >> 8) % (ORE_CELL - 4));
                    int vz = cz * ORE_CELL + 2 + int((h >> 16) % (ORE_CELL - 4));
                    int vy = cy * ORE_CELL + 1 + int((h >> 24) % (ORE_CELL - 1));
                    if (vy > ore.maxY) continue;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dz = -1; dz <= 1; ++dz)
                            for (int dx = -1; dx <= 1; ++dx) {
                                int wy = vy + dy;
                                if (wy < 1 || wy >= CHUNK_HEIGHT) continue;
                                bool core = dx == 0 && dy == 0 && dz == 0;
                                if (!core && (hash2(vx + dx + dy * 64,
                                                    vz + dz, seed_ ^ ore.salt ^ 0xB10Bu)
                                              & 0xFF) >= 110) continue;
                                int lx = vx + dx - bx, lz = vz + dz - bz;
                                if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) continue;
                                if (c.get(lx, wy, lz) == Block::Stone)
                                    c.set(lx, wy, lz, ore.b);
                            }
                }
            }
        }
    }

    // --- Trees ---
    // Consider every tree cell whose blocks could reach into this chunk
    // (leaf radius 2), then write the parts that fall inside.
    int cell0X = floorDiv(bx - 2, TREE_CELL);
    int cell1X = floorDiv(bx + CHUNK_SIZE + 1, TREE_CELL);
    int cell0Z = floorDiv(bz - 2, TREE_CELL);
    int cell1Z = floorDiv(bz + CHUNK_SIZE + 1, TREE_CELL);

    auto setIfInside = [&](int wx, int wy, int wz, Block b, bool overwrite) {
        int lx = wx - bx, lz = wz - bz;
        if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) return;
        if (wy < 0 || wy >= CHUNK_HEIGHT) return;
        if (!overwrite && c.get(lx, wy, lz) != Block::Air) return;
        c.set(lx, wy, lz, b);
    };

    for (int cz = cell0Z; cz <= cell1Z; ++cz) {
        for (int cx = cell0X; cx <= cell1X; ++cx) {
            Tree t = treeInCell(cx, cz);
            if (!t.exists) continue;
            int topY = t.baseY + t.trunkH - 1;
            // Leaf blob: two 5x5 layers below/at the top, then a 3x3 cap and a plus.
            for (int dy = -2; dy <= 1; ++dy) {
                int r = dy <= -1 ? 2 : 1;
                for (int dz = -r; dz <= r; ++dz) {
                    for (int dx = -r; dx <= r; ++dx) {
                        // Trim corners of the wide layers for a rounder look.
                        if (r == 2 && std::abs(dx) == 2 && std::abs(dz) == 2 &&
                            ((hash2(t.x + dx, t.z + dz, seed_ ^ uint32_t(dy)) & 1) == 0))
                            continue;
                        if (dy == 1 && std::abs(dx) + std::abs(dz) > 1) continue; // plus cap
                        setIfInside(t.x + dx, topY + dy, t.z + dz, Block::Leaves, false);
                    }
                }
            }
            // Trunk last so it wins over leaves.
            for (int y = t.baseY; y <= topY; ++y)
                setIfInside(t.x, y, t.z, Block::Wood, true);
        }
    }
}
