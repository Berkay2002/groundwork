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
      hillMask_(seed ^ 0xC0FFEE11u) {}

int Terrain::heightAt(int wx, int wz) const {
    float x = float(wx), z = float(wz);
    // Gentle rolling plains everywhere.
    float plains = plains_.fbm(x, z, 4, 1.0f / 64.0f) * 6.0f;
    // Occasional hill regions selected by a very low-frequency mask.
    float mask = smoothstep01(0.05f, 0.45f, hillMask_.fbm(x, z, 2, 1.0f / 256.0f));
    float hills = (hills_.fbm(x, z, 5, 1.0f / 96.0f) * 0.5f + 0.5f) * 32.0f * mask;
    int h = 24 + (int)std::floor(plains + hills);
    return std::clamp(h, 1, CHUNK_HEIGHT - 8);
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
    t.trunkH = 4 + int((h >> 24) % 3); // 4..6
    t.exists = true;
    return t;
}

void Terrain::generateChunk(Chunk& c) const {
    const int bx = c.cx() * CHUNK_SIZE;
    const int bz = c.cz() * CHUNK_SIZE;

    // --- Ground ---
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            int height = heightAt(bx + x, bz + z);
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
