// Headless tests for core world logic. No GL context required because these
// paths (generation, block access, raycast, persistence) never touch the GPU.
#include "../src/World.h"
#include "../src/Terrain.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

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
                    int wx = ccx * CHUNK_SIZE + x, wz = ccz * CHUNK_SIZE + z;
                    int h = t.heightAt(wx, wz);
                    Block surf = c.get(x, h, z);
                    if (t.isCarved(wx, h, wz, h)) {
                        CHECK(surf == Block::Air); // cave mouth breached the surface
                    } else {
                        Block expected = h <= Terrain::SAND_LEVEL ? Block::Sand : Block::Grass;
                        CHECK(surf == expected);
                    }
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

static void testCaves() {
    Terrain t(1337);
    int carved = 0;
    for (int ccz = -3; ccz <= 3; ++ccz) {
        for (int ccx = -3; ccx <= 3; ++ccx) {
            Chunk c(ccx, ccz);
            t.generateChunk(c);
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    int wx = ccx * CHUNK_SIZE + x, wz = ccz * CHUNK_SIZE + z;
                    int h = t.heightAt(wx, wz);
                    CHECK(c.get(x, 0, z) == Block::Bedrock); // never carved
                    for (int y = 1; y <= h; ++y) {
                        bool carve = t.isCarved(wx, y, wz, h);
                        if (carve) {
                            ++carved;
                            // The generated chunk agrees with the pure predicate
                            // (only leaves, which write into air, may fill a
                            // carved cell near a cave mouth).
                            if (c.get(x, y, z) != Block::Air &&
                                c.get(x, y, z) != Block::Leaves) CHECK(false);
                            // Lake/shore columns keep a solid floor near the surface.
                            CHECK(!(h < Terrain::SEA_LEVEL + 2 && y > h - 4));
                        }
                    }
                }
            }
        }
    }
    CHECK(carved > 200); // caves actually exist in a 7x7 chunk area
}

static void testOres() {
    Terrain t(1337);
    int coal = 0, iron = 0;
    for (int ccz = -3; ccz <= 3; ++ccz) {
        for (int ccx = -3; ccx <= 3; ++ccx) {
            Chunk c(ccx, ccz);
            t.generateChunk(c);
            for (int z = 0; z < CHUNK_SIZE; ++z)
                for (int x = 0; x < CHUNK_SIZE; ++x)
                    for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                        Block b = c.get(x, y, z);
                        if (b != Block::CoalOre && b != Block::IronOre) continue;
                        // Ore replaces stone only, so it stays under the dirt
                        // cap and respects its depth band (vein center max +1).
                        int h = t.heightAt(ccx * CHUNK_SIZE + x, ccz * CHUNK_SIZE + z);
                        CHECK(y < h - 3);
                        if (b == Block::CoalOre) { ++coal; CHECK(y <= Terrain::COAL_MAX_Y + 1); }
                        else                     { ++iron; CHECK(y <= Terrain::IRON_MAX_Y + 1); }
                    }
        }
    }
    CHECK(coal > 100);       // both ores are actually common enough to find
    CHECK(iron > 50);
    CHECK(coal > iron);      // coal has the wider band and higher chance
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

static void testLevelSeed() {
    const char* dir = "test_saves_level";
    std::filesystem::remove_all(dir);
    {
        World w(1234, dir);
        CHECK(w.seed() == 1234); // fresh world adopts the requested seed
    }
    CHECK(std::filesystem::exists(std::string(dir) + "/level.bin"));
    {
        // A different default seed must NOT corrupt an existing save: the
        // world keeps the seed recorded in level.bin.
        World w(9999, dir);
        CHECK(w.seed() == 1234);
    }
    {
        // A corrupt level.bin is rejected and rewritten with the fallback.
        std::ofstream f(std::string(dir) + "/level.bin", std::ios::binary);
        f << "JUNK";
    }
    {
        World w(4321, dir);
        CHECK(w.seed() == 4321);
    }
    {
        World w(1111, dir);
        CHECK(w.seed() == 4321); // the rewrite stuck
    }
    std::filesystem::remove_all(dir);
}

static void testUnloadSaves() {
    const char* dir = "test_saves_unload";
    std::filesystem::remove_all(dir);
    {
        World w(99, dir);
        w.waitUntilLoaded(glm::vec3(8, 40, 8), 1, 5000);
        w.setBlock(5, 40, 5, Block::Stone);
        // Stream far away: chunk (0,0) leaves the keep radius and must be
        // saved on unload, not only at exit.
        w.update(glm::vec3(1000, 40, 1000), 2);
        CHECK(w.getBlock(5, 40, 5) == Block::Air); // chunk really unloaded
        CHECK(std::filesystem::exists(std::string(dir) + "/c_0_0.bin"));
        // No half-written temp files left behind by the atomic writer.
        CHECK(!std::filesystem::exists(std::string(dir) + "/c_0_0.bin.tmp"));
        // Coming back reloads the edit from disk.
        w.waitUntilLoaded(glm::vec3(8, 40, 8), 1, 5000);
        CHECK(w.getBlock(5, 40, 5) == Block::Stone);
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

// Vertex brightness (the 6th float) of vertex v of the n-th emitted face.
static float faceVertLight(const MeshData& md, int face, int v) {
    return md.verts[size_t(face) * 4 * 6 + size_t(v) * 6 + 5];
}

static void testAmbientOcclusion() {
    // Isolated stone block: emits faces f=0..5 in order; the top (+Y) face is
    // the third. With nothing around, all four corners are fully open.
    ChunkSnapshot s;
    s.blocks.assign(Chunk::rawSize(), Block::Air);
    auto at = [](int x, int y, int z) { return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x; };
    s.blocks[at(8, 40, 8)] = Block::Stone;
    MeshData md = buildMeshData(s);
    for (int v = 1; v < 4; ++v)
        CHECK(faceVertLight(md, 2, v) == faceVertLight(md, 2, 0));

    // A diagonal occluder above (+X,+Z corner) darkens exactly the top-face
    // vertex whose corner cell it fills (v1 = (x+1, y+1, z+1)).
    s.blocks[at(9, 41, 9)] = Block::Stone;
    md = buildMeshData(s);
    float open = faceVertLight(md, 2, 0);
    float shaded = faceVertLight(md, 2, 1);
    CHECK(shaded < open);
    CHECK(faceVertLight(md, 2, 2) == open); // other corners untouched
    CHECK(faceVertLight(md, 2, 3) == open);
}

static void testAOCornerColumn() {
    // Cross-chunk AO: a block at the chunk's (-X,-Z) corner shaded by a
    // diagonal neighbor supplied via the snapshot's corner column.
    ChunkSnapshot s;
    s.blocks.assign(Chunk::rawSize(), Block::Air);
    s.blocks[(40 * CHUNK_SIZE + 0) * CHUNK_SIZE + 0] = Block::Stone;
    MeshData md = buildMeshData(s);
    float open = faceVertLight(md, 2, 3); // top-face vertex at (0, 41, 0)
    s.cornerXnZn.assign(CHUNK_HEIGHT, Block::Air);
    s.cornerXnZn[41] = Block::Stone; // diagonal cell (-1, 41, -1)
    md = buildMeshData(s);
    CHECK(faceVertLight(md, 2, 3) < open);
    CHECK(faceVertLight(md, 2, 1) == open); // opposite corner unaffected
}

static void testSmoothLighting() {
    // Explicit light data: only the cell straight above the block is sunlit;
    // the corners average it with their dark side/diagonal cells, so every
    // top-face vertex lands strictly between dark and full brightness.
    ChunkSnapshot s;
    s.blocks.assign(Chunk::rawSize(), Block::Air);
    s.light.assign(Chunk::rawSize(), 0);
    auto at = [](int x, int y, int z) { return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x; };
    s.blocks[at(8, 40, 8)] = Block::Stone;
    s.light[at(8, 41, 8)] = 15; // sun nibble
    MeshData md = buildMeshData(s);
    float v0 = faceVertLight(md, 2, 0);
    CHECK(v0 > 0.1f && v0 < 1.0f);

    // Lighting the +X neighbor cell too brightens the two +X-side corners
    // (v1, v2) relative to the -X ones (v0, v3): the gradient interpolates.
    s.light[at(9, 41, 8)] = 15;
    md = buildMeshData(s);
    CHECK(faceVertLight(md, 2, 1) > faceVertLight(md, 2, 0));
    CHECK(faceVertLight(md, 2, 2) > faceVertLight(md, 2, 3));
}

static void testBlockRegistry() {
    // Every row is filled in and internally consistent.
    for (int i = 0; i < BLOCK_TYPES; ++i) {
        Block b = Block(i);
        const BlockDef& d = blockDef(b);
        CHECK(d.name != nullptr && d.name[0] != '\0');
        for (int f = 0; f < 6; ++f) CHECK(d.tiles[f] < ATLAS_TILES);
        CHECK(d.emission <= 15);
        CHECK(uint8_t(d.drop) < BLOCK_TYPES);
        CHECK(!d.collidable || d.solid);   // collidable implies solid
        CHECK(!d.opaque || d.solid);       // opaque implies solid
        CHECK(!(d.opaque && d.dimsSunlight)); // dimsSunlight is for transparents
    }
    // Spot-check the semantics the old switch-based predicates encoded.
    CHECK(!isSolid(Block::Air));
    CHECK(isSolid(Block::Torch) && !isCollidable(Block::Torch) && !isOpaque(Block::Torch));
    CHECK(lightEmission(Block::Torch) == 14);
    CHECK(!isBreakable(Block::Bedrock) && isSolid(Block::Bedrock));
    CHECK(isBreakable(Block::Stone));
    CHECK(tileFor(Block::Grass, 2) == 0);  // top
    CHECK(tileFor(Block::Grass, 3) == 2);  // bottom = dirt
    CHECK(tileFor(Block::Grass, 0) == 1);  // side
    CHECK(tileFor(Block::Wood, 2) == 5 && tileFor(Block::Wood, 4) == 4);
    CHECK(std::string(blockName(Block::CoalOre)) == "Coal Ore");
}

static void testWaterPredicates() {
    CHECK(!isSolid(Block::Water));      // raycast/placement pass through
    CHECK(!isCollidable(Block::Water)); // swim-through
    CHECK(!isOpaque(Block::Water));     // light + lakebed faces pass
    CHECK(dimsSunlight(Block::Water));
    CHECK(!dimsSunlight(Block::Air));
    CHECK(!dimsSunlight(Block::Stone)); // opaque, but not via this predicate
}

// Find a lake column (height comfortably under SEA_LEVEL) near the origin.
static bool findLake(const Terrain& t, int& lx, int& lz) {
    lx = lz = 0;
    for (int z = -200; z <= 200; z += 2)
        for (int x = -200; x <= 200; x += 2)
            if (t.heightAt(x, z) <= Terrain::SEA_LEVEL - 3) { lx = x; lz = z; return true; }
    return false;
}

static void testWaterGeneration() {
    Terrain t(1337);
    int lx, lz;
    CHECK(findLake(t, lx, lz));
    int h = t.heightAt(lx, lz);
    Chunk c(World::floorDiv(lx, CHUNK_SIZE), World::floorDiv(lz, CHUNK_SIZE));
    t.generateChunk(c);
    int x = World::mod(lx, CHUNK_SIZE), z = World::mod(lz, CHUNK_SIZE);
    CHECK(c.get(x, h, z) == Block::Sand); // sandy lakebed
    for (int y = h + 1; y <= Terrain::SEA_LEVEL; ++y)
        CHECK(c.get(x, y, z) == Block::Water);
    CHECK(c.get(x, Terrain::SEA_LEVEL + 1, z) == Block::Air);
}

static void testWaterMesh() {
    // A lone water block: all 6 faces, in the water arrays only.
    ChunkSnapshot s;
    s.blocks.assign(Chunk::rawSize(), Block::Air);
    auto at = [](int x, int y, int z) { return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x; };
    s.blocks[at(8, 40, 8)] = Block::Water;
    MeshData md = buildMeshData(s);
    CHECK(md.inds.empty());
    CHECK(md.waterInds.size() == 6u * 6);

    // Water on stone: the shared pair is one stone face (drawn, water is not
    // opaque) and one water face (culled against opaque).
    s.blocks[at(8, 39, 8)] = Block::Stone;
    md = buildMeshData(s);
    CHECK(md.inds.size() == 6u * 6);      // all stone faces incl. under water
    CHECK(md.waterInds.size() == 5u * 6); // water bottom culled

    // Stacked water culls the water-water pair.
    s.blocks[at(8, 39, 8)] = Block::Water;
    md = buildMeshData(s);
    CHECK(md.inds.empty());
    CHECK(md.waterInds.size() == 10u * 6);
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

static void testWaterLight() {
    Terrain t(1337);
    int lx, lz;
    CHECK(findLake(t, lx, lz));
    const char* dir = "test_saves_water";
    std::filesystem::remove_all(dir);
    {
    World w(1337, dir);
    w.waitUntilLoaded(glm::vec3(lx, 40, lz), 1, 5000);
    // Open sky above the lake; the top water cell loses the lossless rule.
    CHECK(w.sunLightAt(lx, Terrain::SEA_LEVEL + 1, lz) == 15);
    CHECK(w.sunLightAt(lx, Terrain::SEA_LEVEL, lz) == 14);
    CHECK(w.sunLightAt(lx, Terrain::SEA_LEVEL - 1, lz) == 13);
    CHECK(w.getBlock(lx, Terrain::SEA_LEVEL - 1, lz) == Block::Water); // depth >= 2

    // Placing water in the open mid-air dims the column the same way, and
    // removing it restores full sun (incremental relight matches initial).
    int gy = Terrain::SEA_LEVEL + 5; // open air above the lake surface
    CHECK(w.getBlock(lx, gy, lz) == Block::Air);
    w.setBlock(lx, gy, lz, Block::Water);
    CHECK(w.sunLightAt(lx, gy, lz) == 14);     // inside the water cell
    CHECK(w.sunLightAt(lx, gy - 1, lz) == 14); // sideways refill from open sky
    w.setBlock(lx, gy, lz, Block::Air);
    CHECK(w.sunLightAt(lx, gy, lz) == 15);
    CHECK(w.sunLightAt(lx, gy - 1, lz) == 15);
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
    testAmbientOcclusion();
    testAOCornerColumn();
    testSmoothLighting();
    testBlockRegistry();
    testWaterPredicates();
    testWaterGeneration();
    testWaterMesh();
    testWaterLight();
    testSunlight();
    testTorchLight();
    testCrossBorderTorch();
    testTerrainDeterminism();
    testTerrainShape();
    testTreeBorderConsistency();
    testCaves();
    testOres();
    testSaveLoadRoundTrip();
    testLevelSeed();
    testUnloadSaves();
    testRaycast();
    if (failures == 0) std::printf("all tests passed\n");
    return failures == 0 ? 0 : 1;
}
