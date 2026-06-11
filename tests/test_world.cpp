// Headless tests for core world logic. No GL context required because these
// paths (generation, block access, raycast, persistence) never touch the GPU.
#include "../src/World.h"
#include "../src/Terrain.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static void testFloorDivMod() {
    CHECK(World::floorDiv(17, 16) == 1);
    CHECK(World::floorDiv(-1, 16) == -1);
    CHECK(World::floorDiv(-16, 16) == -1);
    CHECK(World::floorDiv(-17, 16) == -2);
    CHECK(World::mod(-1, 16) == 15);
    CHECK(World::mod(17, 16) == 1);
    CHECK(World::mod(-16, 16) == 0);
}

static void testTerrainDeterminism() {
    Terrain t1(42), t2(42);
    Chunk a(3, -2), b(3, -2);
    t1.generateChunk(a);
    t2.generateChunk(b);
    for (int y = 0; y < CHUNK_HEIGHT; ++y)
        for (int z = 0; z < CHUNK_SIZE; ++z)
            for (int x = 0; x < CHUNK_SIZE; ++x)
                if (a.get(x, y, z) != b.get(x, y, z)) { CHECK(false); return; }
    CHECK(true);
    CHECK(t1.heightAt(-1234, 5678) == t2.heightAt(-1234, 5678));
}

static void testTerrainShape() {
    Terrain t(1337);
    // Scan a few chunks: bedrock floor, surface matches heightAt and the
    // sand rule, and tree trunks stand on the surface.
    int woodSeen = 0, leavesSeen = 0;
    for (int ccz = -2; ccz <= 2; ++ccz) {
        for (int ccx = -2; ccx <= 2; ++ccx) {
            Chunk c(ccx, ccz);
            t.generateChunk(c);
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    CHECK(c.get(x, 0, z) == Block::Bedrock);
                    int h = t.heightAt(ccx * CHUNK_SIZE + x, ccz * CHUNK_SIZE + z);
                    Block surf = c.get(x, h, z);
                    Block expected = h <= Terrain::SAND_LEVEL ? Block::Sand : Block::Grass;
                    CHECK(surf == expected);
                    for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                        Block b = c.get(x, y, z);
                        if (b == Block::Wood) {
                            ++woodSeen;
                            Block below = c.get(x, y - 1, z);
                            CHECK(below == Block::Wood || below == Block::Grass);
                        }
                        if (b == Block::Leaves) ++leavesSeen;
                    }
                }
            }
        }
    }
    CHECK(woodSeen > 0);
    CHECK(leavesSeen > woodSeen); // canopies are bigger than trunks
}

static void testTreeBorderConsistency() {
    // A chunk's tree blocks must not depend on which chunk generates them:
    // every Wood/Leaves block in chunk (0,0) must belong to a tree whose cell
    // candidate (queried independently) covers that block.
    Terrain t(7777);
    Chunk c(0, 0);
    t.generateChunk(c);
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            for (int y = 1; y < CHUNK_HEIGHT; ++y) {
                Block b = c.get(x, y, z);
                if (b != Block::Wood && b != Block::Leaves) continue;
                bool covered = false;
                for (int cz = World::floorDiv(z - 2, Terrain::TREE_CELL);
                     cz <= World::floorDiv(z + 2, Terrain::TREE_CELL) && !covered; ++cz)
                    for (int cx = World::floorDiv(x - 2, Terrain::TREE_CELL);
                         cx <= World::floorDiv(x + 2, Terrain::TREE_CELL) && !covered; ++cx) {
                        Terrain::Tree tr = t.treeInCell(cx, cz);
                        if (!tr.exists) continue;
                        if (std::abs(tr.x - x) <= 2 && std::abs(tr.z - z) <= 2 &&
                            y >= tr.baseY - 1 && y <= tr.baseY + tr.trunkH + 1)
                            covered = true;
                    }
                CHECK(covered);
            }
        }
    }
}

static void testSaveLoadRoundTrip() {
    const char* dir = "test_saves_tmp";
    std::filesystem::remove_all(dir);
    {
        World w(99, dir);
        w.waitUntilLoaded(glm::vec3(8, 40, 8), 1, 5000);
        w.setBlock(5, 40, 5, Block::Stone);
        w.setBlock(5, 41, 5, Block::Air);
        w.saveAllModified();
    }
    {
        World w(99, dir);
        w.waitUntilLoaded(glm::vec3(8, 40, 8), 1, 5000);
        CHECK(w.getBlock(5, 40, 5) == Block::Stone);
        CHECK(w.getBlock(5, 41, 5) == Block::Air);
    }
    std::filesystem::remove_all(dir);
}

static void testRaycast() {
    const char* dir = "test_saves_tmp2";
    std::filesystem::remove_all(dir);
    World w(7, dir);
    w.waitUntilLoaded(glm::vec3(8, 40, 8), 1, 5000);

    // Find the surface at (8, ?, 8) and cast straight down at it.
    int top = -1;
    for (int y = CHUNK_HEIGHT - 1; y >= 0; --y)
        if (isSolid(w.getBlock(8, y, 8))) { top = y; break; }
    CHECK(top > 0);
    RaycastHit hit = w.raycast(glm::vec3(8.5f, top + 3.0f, 8.5f), glm::vec3(0, -1, 0), 10.0f);
    CHECK(hit.hit);
    CHECK(hit.block == glm::ivec3(8, top, 8));
    CHECK(hit.adjacent == glm::ivec3(8, top + 1, 8));

    // A ray pointing at the sky hits nothing.
    RaycastHit miss = w.raycast(glm::vec3(8.5f, top + 3.0f, 8.5f), glm::vec3(0, 1, 0), 10.0f);
    CHECK(!miss.hit);
    std::filesystem::remove_all(dir);
}

static void testMeshData() {
    // One stone block floating in an empty snapshot -> exactly 6 faces.
    ChunkSnapshot s;
    s.blocks.assign(Chunk::rawSize(), Block::Air);
    s.blocks[(40 * CHUNK_SIZE + 8) * CHUNK_SIZE + 8] = Block::Stone;
    MeshData md = buildMeshData(s);
    CHECK(md.verts.size() == 6u * 4 * 6); // 6 faces * 4 verts * 6 floats
    CHECK(md.inds.size() == 6u * 6);      // 6 faces * 6 indices

    // Two stacked blocks share a hidden face pair -> 10 faces total.
    s.blocks[(41 * CHUNK_SIZE + 8) * CHUNK_SIZE + 8] = Block::Stone;
    md = buildMeshData(s);
    CHECK(md.inds.size() == 10u * 6);

    // A block at the -X border with a solid neighbor edge hides that face.
    s.blocks.assign(Chunk::rawSize(), Block::Air);
    s.blocks[(40 * CHUNK_SIZE + 8) * CHUNK_SIZE + 0] = Block::Stone;
    md = buildMeshData(s);
    CHECK(md.inds.size() == 6u * 6); // empty edge = air, all faces drawn
    s.edgeXn.assign(size_t(CHUNK_HEIGHT) * CHUNK_SIZE, Block::Stone);
    md = buildMeshData(s);
    CHECK(md.inds.size() == 5u * 6);
}

static int surfaceY(World& w, int x, int z) {
    for (int y = CHUNK_HEIGHT - 1; y >= 0; --y)
        if (isSolid(w.getBlock(x, y, z))) return y;
    return -1;
}

static void testSunlight() {
    const char* dir = "test_saves_light";
    std::filesystem::remove_all(dir);
    { // scope: ~World saves modified chunks, so it must run before cleanup
    World w(1337, dir);
    w.waitUntilLoaded(glm::vec3(8, 40, 8), 1, 5000);
    int top = surfaceY(w, 8, 8);
    CHECK(top > 0);
    CHECK(w.sunLightAt(8, top + 1, 8) == 15);   // open sky
    CHECK(w.sunLightAt(8, top, 8) == 0);        // inside the surface block
    CHECK(w.blockLightAt(8, top + 1, 8) == 0);  // no emitters in raw terrain

    // A floating block shadows the column below to 14 (sideways refill)...
    w.setBlock(8, top + 8, 8, Block::Stone);
    CHECK(w.sunLightAt(8, top + 9, 8) == 15);
    CHECK(w.sunLightAt(8, top + 7, 8) == 14);
    CHECK(w.sunLightAt(8, top + 1, 8) == 14);
    // ...and breaking it restores full sun all the way down.
    w.setBlock(8, top + 8, 8, Block::Air);
    CHECK(w.sunLightAt(8, top + 7, 8) == 15);
    CHECK(w.sunLightAt(8, top + 1, 8) == 15);
    }
    std::filesystem::remove_all(dir);
}

static void testTorchLight() {
    const char* dir = "test_saves_torch";
    std::filesystem::remove_all(dir);
    {
    World w(1337, dir);
    w.waitUntilLoaded(glm::vec3(8, 40, 8), 1, 5000);
    int top = surfaceY(w, 8, 8);
    w.setBlock(8, top + 1, 8, Block::Torch);
    CHECK(w.blockLightAt(8, top + 1, 8) == 14); // the torch cell itself
    CHECK(w.blockLightAt(8, top + 2, 8) == 13); // one step of attenuation
    CHECK(w.blockLightAt(8, top + 3, 8) == 12);
    CHECK(w.sunLightAt(8, top + 1, 8) == 15);   // torch doesn't block sun
    w.setBlock(8, top + 1, 8, Block::Air);
    CHECK(w.blockLightAt(8, top + 1, 8) == 0);  // unlight BFS cleaned up
    CHECK(w.blockLightAt(8, top + 2, 8) == 0);
    CHECK(w.sunLightAt(8, top + 1, 8) == 15);   // sun column restored
    }
    std::filesystem::remove_all(dir);
}

static void testCrossBorderTorch() {
    const char* dir = "test_saves_border";
    std::filesystem::remove_all(dir);
    {
    World w(1337, dir);
    w.waitUntilLoaded(glm::vec3(0, 40, 8), 1, 5000); // chunks (-1..1, -1..1)
    int top = surfaceY(w, 0, 8);
    // Make sure the cell across the chunk border is air, then light it.
    if (isSolid(w.getBlock(-1, top + 1, 8))) w.setBlock(-1, top + 1, 8, Block::Air);
    w.setBlock(0, top + 1, 8, Block::Torch);
    CHECK(w.blockLightAt(-1, top + 1, 8) == 13); // crossed into chunk (-1,0)
    }
    std::filesystem::remove_all(dir);
}

int main() {
    testFloorDivMod();
    testMeshData();
    testSunlight();
    testTorchLight();
    testCrossBorderTorch();
    testTerrainDeterminism();
    testTerrainShape();
    testTreeBorderConsistency();
    testSaveLoadRoundTrip();
    testRaycast();
    if (failures == 0) std::printf("all tests passed\n");
    return failures == 0 ? 0 : 1;
}
