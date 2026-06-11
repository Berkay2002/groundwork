// Headless tests for core world logic. No GL context required because these
// paths (generation, block access, raycast, persistence) never touch the GPU.
#include "world/World.h"
#include "world/Terrain.h"
#include "sim/Physics.h"
#include "sim/Player.h"
#include "sim/Item.h"
#include "sim/Inventory.h"
#include "sim/Entity.h"
#include "sim/Mining.h"
#include "sim/Crafting.h"
#include "sim/PlayerSave.h"
#include "world/WorldSave.h"
#include "world/BlockEntity.h"
#include "world/DayCycle.h"
#include "world/Lighting.h"
#include "platform/Settings.h"
#include "audio/Sounds.h"
#include "ui/MenuUi.h"
#include "render/Texture.h"
#include "render/BreakOverlay.h"
#include "platform/SaveIO.h"
#include "sim/TickClock.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static_assert(ITEM_TYPES == 34, "ItemId is saved data; append ids only");
static_assert(BLOCK_TYPES == 17, "Block ids are saved data; append ids only");

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
    int coal = 0, iron = 0, diamond = 0;
    for (int ccz = -8; ccz <= 8; ++ccz) {
        for (int ccx = -8; ccx <= 8; ++ccx) {
            Chunk c(ccx, ccz);
            t.generateChunk(c);
            for (int z = 0; z < CHUNK_SIZE; ++z)
                for (int x = 0; x < CHUNK_SIZE; ++x)
                    for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                        Block b = c.get(x, y, z);
                        if (b != Block::CoalOre && b != Block::IronOre &&
                            b != Block::DiamondOre) continue;
                        // Ore replaces stone only, so it stays under the dirt
                        // cap and respects its depth band (vein center max +1).
                        int h = t.heightAt(ccx * CHUNK_SIZE + x, ccz * CHUNK_SIZE + z);
                        CHECK(y < h - 3);
                        if (b == Block::CoalOre) { ++coal; CHECK(y <= Terrain::COAL_MAX_Y + 1); }
                        else if (b == Block::IronOre) { ++iron; CHECK(y <= Terrain::IRON_MAX_Y + 1); }
                        else { ++diamond; CHECK(y <= Terrain::DIAMOND_MAX_Y + 1); }
                    }
        }
    }
    CHECK(coal > 100);       // both ores are actually common enough to find
    CHECK(iron > 50);
    CHECK(diamond > 10);
    CHECK(coal > iron);      // coal has the wider band and higher chance
    CHECK(iron > diamond);   // diamond exists, but is rarer and deeper
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

static void testWorldSaveChunkFormat() {
    const char* dir = "test_worldsave_chunk";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string path = worldsave::chunkPath(dir, -2, 3);

    std::vector<uint8_t> blocks(Chunk::rawSize(), uint8_t(Block::Air));
    blocks[0] = uint8_t(Block::Stone);
    blocks[1] = 255; // file from a newer build/corruption: clamp to Air on load
    CHECK(worldsave::saveChunkFile(path, blocks.data(), blocks.size()));

    std::vector<uint8_t> loaded(Chunk::rawSize(), 0);
    CHECK(worldsave::loadChunkFile(path, loaded.data(), loaded.size()));
    CHECK(loaded[0] == uint8_t(Block::Stone));
    CHECK(loaded[1] == uint8_t(Block::Air));

    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << "JUNK";
    }
    CHECK(!worldsave::loadChunkFile(path, loaded.data(), loaded.size()));
    std::filesystem::remove_all(dir);
}

static void testWorldSaveLevelFormat() {
    const char* dir = "test_worldsave_level";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string path = std::string(dir) + "/level.bin";

    CHECK(worldsave::saveLevelFile(path, 1234, 42.5f));
    worldsave::LevelFile level = worldsave::loadLevelFile(path);
    CHECK(level.ok);
    CHECK(level.seed == 1234);
    CHECK(std::fabs(level.dayTime - 42.5f) < 1e-6f);

    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        constexpr char magic[4] = {'M', 'C', 'L', 'V'};
        uint32_t version = 1, seed = 777;
        f.write(magic, 4);
        f.write(reinterpret_cast<const char*>(&version), 4);
        f.write(reinterpret_cast<const char*>(&seed), 4);
    }
    level = worldsave::loadLevelFile(path);
    CHECK(level.ok);
    CHECK(level.seed == 777);
    CHECK(level.dayTime == 0.0f);

    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << "BAD!";
    }
    CHECK(!worldsave::loadLevelFile(path).ok);
    std::filesystem::remove_all(dir);
}

static void testAtomicSaveOverwritesExisting() {
    const char* dir = "test_atomic_save";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string path = std::string(dir) + "/file.bin";

    CHECK(atomicSave(path, [](std::ofstream& f) { f << "old"; }));
    CHECK(atomicSave(path, [](std::ofstream& f) { f << "new-data"; }));
    CHECK(!std::filesystem::exists(path + ".tmp"));

    {
        std::ifstream f(path, std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        CHECK(data == "new-data");
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

// --- Packed-mesh helpers. A quad is 4 consecutive ChunkVertex; positions are
// chunk-local in 1/16-block units (block coordinate * 16). Greedy meshing
// makes emission order an implementation detail, so tests locate quads by
// the positions of their vertices instead.

// Index of the unique quad whose four vertices all satisfy pred (-1 = none,
// -2 = ambiguous).
template <typename Pred>
static int findQuad(const std::vector<ChunkVertex>& vs, Pred pred) {
    int found = -1;
    for (size_t q = 0; q + 3 < vs.size(); q += 4) {
        if (pred(vs[q]) && pred(vs[q + 1]) && pred(vs[q + 2]) && pred(vs[q + 3])) {
            if (found >= 0) return -2;
            found = int(q / 4);
        }
    }
    return found;
}

// Daytime brightness of a vertex (sun channel at full scale vs block light)
// — identical to the single combined light byte the format had pre-day/night.
static int dayLight(const ChunkVertex& v) { return std::max(v.sun, v.blk); }

// Light byte of the vertex at exact packed position (x,y,z) inside quad q.
static int lightAtVert(const std::vector<ChunkVertex>& vs, int quad, int x, int y, int z) {
    for (int i = 0; i < 4; ++i) {
        const ChunkVertex& v = vs[size_t(quad) * 4 + i];
        if (v.x == x && v.y == y && v.z == z) return dayLight(v);
    }
    return -1;
}

// The top (+Y) face quad of the block at (bx, by, bz): all four verts in the
// y = by+1 plane within the block's footprint.
static int topQuadOf(const std::vector<ChunkVertex>& vs, int bx, int by, int bz) {
    return findQuad(vs, [&](const ChunkVertex& v) {
        return v.y == (by + 1) * 16 &&
               v.x >= bx * 16 && v.x <= (bx + 1) * 16 &&
               v.z >= bz * 16 && v.z <= (bz + 1) * 16;
    });
}

static void testMeshData() {
    // One stone block floating in an empty snapshot -> exactly 6 quads.
    ChunkSnapshot s;
    s.blocks.assign(Chunk::rawSize(), Block::Air);
    s.blocks[(40 * CHUNK_SIZE + 8) * CHUNK_SIZE + 8] = Block::Stone;
    MeshData md = buildMeshData(s);
    CHECK(md.verts.size() == 6u * 4); // 6 quads * 4 packed verts
    CHECK(md.inds.size() == 6u * 6);  // 6 quads * 6 indices

    // Two stacked blocks: the shared face pair is culled and, in open air,
    // each 2-cell side column merges into one greedy quad -> 6 quads total.
    s.blocks[(41 * CHUNK_SIZE + 8) * CHUNK_SIZE + 8] = Block::Stone;
    md = buildMeshData(s);
    CHECK(md.inds.size() == 6u * 6);
    // The merged +X side spans both cells: y from 40 to 42, v from 0 to 2 tiles.
    int side = findQuad(md.verts, [](const ChunkVertex& v) { return v.x == 9 * 16; });
    CHECK(side >= 0);
    int ymin = 1 << 16, ymax = -1, vmax = -1;
    for (int i = 0; i < 4; ++i) {
        const ChunkVertex& v = md.verts[size_t(side) * 4 + i];
        ymin = std::min(ymin, int(v.y)); ymax = std::max(ymax, int(v.y));
        vmax = std::max(vmax, int(v.v));
    }
    CHECK(ymin == 40 * 16 && ymax == 42 * 16);
    CHECK(vmax == 2 * 16); // texture v tiles twice across the merged quad

    // A block at the -X border with a solid neighbor edge hides that face.
    s.blocks.assign(Chunk::rawSize(), Block::Air);
    s.blocks[(40 * CHUNK_SIZE + 8) * CHUNK_SIZE + 0] = Block::Stone;
    md = buildMeshData(s);
    CHECK(md.inds.size() == 6u * 6); // empty edge = air, all faces drawn
    s.edgeXn.assign(size_t(CHUNK_HEIGHT) * CHUNK_SIZE, Block::Stone);
    md = buildMeshData(s);
    CHECK(md.inds.size() == 5u * 6);
}

static void testGreedyMerging() {
    // A 4x4 stone slab floating in fully sunlit open air: every face of the
    // slab is uniformly lit and unoccluded, so greedy meshing collapses it to
    // exactly 6 quads (one per direction), same as a single block.
    ChunkSnapshot s;
    s.blocks.assign(Chunk::rawSize(), Block::Air);
    auto at = [](int x, int y, int z) { return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x; };
    for (int z = 4; z < 8; ++z)
        for (int x = 4; x < 8; ++x)
            s.blocks[at(x, 40, z)] = Block::Stone;
    MeshData md = buildMeshData(s);
    CHECK(md.verts.size() == 6u * 4);
    // The top quad covers the whole slab and tiles its texture 4x4.
    int top = findQuad(md.verts, [](const ChunkVertex& v) { return v.y == 41 * 16; });
    CHECK(top >= 0);
    int umax = -1, vmax = -1;
    for (int i = 0; i < 4; ++i) {
        const ChunkVertex& v = md.verts[size_t(top) * 4 + i];
        CHECK(v.sun == 255); // full sun, AO open, top shade 1.0
        CHECK(v.layer == 3);   // stone tile
        umax = std::max(umax, int(v.u)); vmax = std::max(vmax, int(v.v));
    }
    CHECK(umax == 4 * 16 && vmax == 4 * 16);

    // A block sitting on one corner of the slab casts AO onto the top faces
    // around it: those cells' corner tuples differ, so the top no longer
    // merges into a single quad and darkened vertices appear.
    s.blocks[at(4, 41, 4)] = Block::Stone;
    md = buildMeshData(s);
    CHECK(md.verts.size() > 6u * 4);
    bool darkened = false;
    for (size_t i = 0; i < md.verts.size(); ++i)
        if (md.verts[i].y == 41 * 16 && dayLight(md.verts[i]) < 255) darkened = true;
    CHECK(darkened);

    // Same input twice -> bit-identical mesh (worker scheduling can't change
    // results because meshing is a pure function of the snapshot).
    MeshData md2 = buildMeshData(s);
    CHECK(md.verts.size() == md2.verts.size() && md.inds == md2.inds);
    CHECK(std::memcmp(md.verts.data(), md2.verts.data(),
                      md.verts.size() * sizeof(ChunkVertex)) == 0);
}

static void testAmbientOcclusion() {
    // Isolated stone block: with nothing around, all four corners of the top
    // face are fully open and equally bright.
    ChunkSnapshot s;
    s.blocks.assign(Chunk::rawSize(), Block::Air);
    auto at = [](int x, int y, int z) { return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x; };
    s.blocks[at(8, 40, 8)] = Block::Stone;
    MeshData md = buildMeshData(s);
    int top = topQuadOf(md.verts, 8, 40, 8);
    CHECK(top >= 0);
    int open = lightAtVert(md.verts, top, 8 * 16, 41 * 16, 8 * 16);
    CHECK(open > 0);
    for (int i = 1; i < 4; ++i)
        CHECK(dayLight(md.verts[size_t(top) * 4 + i]) == dayLight(md.verts[size_t(top) * 4]));

    // A diagonal occluder above the (+X,+Z) corner darkens exactly the
    // top-face vertex whose corner cell it fills.
    s.blocks[at(9, 41, 9)] = Block::Stone;
    md = buildMeshData(s);
    top = topQuadOf(md.verts, 8, 40, 8);
    CHECK(top >= 0);
    CHECK(lightAtVert(md.verts, top, 9 * 16, 41 * 16, 9 * 16) < open);
    CHECK(lightAtVert(md.verts, top, 8 * 16, 41 * 16, 8 * 16) == open);
    CHECK(lightAtVert(md.verts, top, 9 * 16, 41 * 16, 8 * 16) == open);
    CHECK(lightAtVert(md.verts, top, 8 * 16, 41 * 16, 9 * 16) == open);
}

static void testAOCornerColumn() {
    // Cross-chunk AO: a block at the chunk's (-X,-Z) corner shaded by a
    // diagonal neighbor supplied via the snapshot's corner column.
    ChunkSnapshot s;
    s.blocks.assign(Chunk::rawSize(), Block::Air);
    s.blocks[(40 * CHUNK_SIZE + 0) * CHUNK_SIZE + 0] = Block::Stone;
    MeshData md = buildMeshData(s);
    int top = topQuadOf(md.verts, 0, 40, 0);
    CHECK(top >= 0);
    int open = lightAtVert(md.verts, top, 0, 41 * 16, 0);
    s.cornerXnZn.assign(CHUNK_HEIGHT, Block::Air);
    s.cornerXnZn[41] = Block::Stone; // diagonal cell (-1, 41, -1)
    md = buildMeshData(s);
    top = topQuadOf(md.verts, 0, 40, 0);
    CHECK(top >= 0);
    CHECK(lightAtVert(md.verts, top, 0, 41 * 16, 0) < open);
    CHECK(lightAtVert(md.verts, top, 16, 41 * 16, 16) == open); // opposite corner
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
    int top = topQuadOf(md.verts, 8, 40, 8);
    CHECK(top >= 0);
    int l = lightAtVert(md.verts, top, 8 * 16, 41 * 16, 8 * 16);
    CHECK(l > 26 && l < 255); // 26 = floor light, 255 = full brightness

    // Lighting the +X neighbor cell too brightens the two +X-side corners
    // relative to the -X ones: the gradient interpolates across the face.
    s.light[at(9, 41, 8)] = 15;
    md = buildMeshData(s);
    top = topQuadOf(md.verts, 8, 40, 8);
    CHECK(top >= 0);
    CHECK(lightAtVert(md.verts, top, 9 * 16, 41 * 16, 8 * 16) >
          lightAtVert(md.verts, top, 8 * 16, 41 * 16, 8 * 16));
    CHECK(lightAtVert(md.verts, top, 9 * 16, 41 * 16, 9 * 16) >
          lightAtVert(md.verts, top, 8 * 16, 41 * 16, 9 * 16));
}

static void testBlockRegistry() {
    // Every row is filled in and internally consistent.
    for (int i = 0; i < BLOCK_TYPES; ++i) {
        Block b = Block(i);
        const BlockDef& d = blockDef(b);
        CHECK(d.name != nullptr && d.name[0] != '\0');
        for (int f = 0; f < 6; ++f) CHECK(int(d.tiles[f]) < ATLAS_TILES);
        CHECK(d.emission <= 15);
        CHECK(uint8_t(d.drop) < BLOCK_TYPES);
        CHECK(uint16_t(d.dropItem) < ITEM_TYPES);
        CHECK(uint16_t(d.wrongToolDropItem) < ITEM_TYPES);
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
    CHECK(std::string(blockName(Block::Wood)) == "Log");
    CHECK(std::string(blockName(Block::Cobblestone)) == "Cobblestone");
    CHECK(blockDef(Block::Stone).preferredTool == ToolClass::Pickaxe);
    CHECK(blockDef(Block::Stone).minHarvestTier == ToolTier::Wood);
    CHECK(blockDef(Block::Stone).dropItem == ItemId::CobblestoneBlock);
    CHECK(blockDef(Block::CoalOre).dropItem == ItemId::Coal);
    CHECK(blockDef(Block::IronOre).dropItem == ItemId::RawIron);
    CHECK(blockDef(Block::IronOre).minHarvestTier == ToolTier::Stone);
    CHECK(blockDef(Block::DiamondOre).dropItem == ItemId::Diamond);
    CHECK(blockDef(Block::DiamondOre).minHarvestTier == ToolTier::Iron);
    CHECK(blockDef(Block::Dirt).preferredTool == ToolClass::Shovel);
    CHECK(blockDef(Block::Sand).preferredTool == ToolClass::Shovel);
    CHECK(blockDef(Block::Wood).preferredTool == ToolClass::Axe);
    CHECK(blockDef(Block::Planks).preferredTool == ToolClass::Axe);
    CHECK(blockDef(Block::CraftingTable).preferredTool == ToolClass::Axe);
    CHECK(blockDef(Block::Furnace).preferredTool == ToolClass::Pickaxe);
    CHECK(blockDef(Block::Furnace).wrongToolDropItem == ItemId::None);
    CHECK(tileFor(Block::Cobblestone, 0) < ATLAS_TILES);
    CHECK(tileFor(Block::Planks, 0) < ATLAS_TILES);
    CHECK(tileFor(Block::CraftingTable, 2) < ATLAS_TILES);
    CHECK(tileFor(Block::Furnace, 4) < ATLAS_TILES);
    CHECK(tileFor(Block::DiamondOre, 0) < ATLAS_TILES);

    struct ExpectedHarvestRow {
        Block block;
        float hardness;
        ToolClass preferredTool;
        ToolTier minTier;
        ItemId drop;
        uint8_t dropCount;
        ItemId wrongDrop;
        uint8_t wrongDropCount;
    };
    const ExpectedHarvestRow rows[] = {
        {Block::Grass, 0.6f, ToolClass::Shovel, ToolTier::Hand, ItemId::DirtBlock, 1, ItemId::DirtBlock, 1},
        {Block::Dirt, 0.5f, ToolClass::Shovel, ToolTier::Hand, ItemId::DirtBlock, 1, ItemId::DirtBlock, 1},
        {Block::Sand, 0.5f, ToolClass::Shovel, ToolTier::Hand, ItemId::SandBlock, 1, ItemId::SandBlock, 1},
        {Block::Wood, 2.0f, ToolClass::Axe, ToolTier::Hand, ItemId::LogBlock, 1, ItemId::LogBlock, 1},
        {Block::Leaves, 0.2f, ToolClass::None, ToolTier::Hand, ItemId::None, 0, ItemId::None, 0},
        {Block::Torch, 0.0f, ToolClass::None, ToolTier::Hand, ItemId::TorchBlock, 1, ItemId::TorchBlock, 1},
        {Block::Planks, 2.0f, ToolClass::Axe, ToolTier::Hand, ItemId::PlanksBlock, 1, ItemId::PlanksBlock, 1},
        {Block::CraftingTable, 2.5f, ToolClass::Axe, ToolTier::Hand, ItemId::CraftingTableBlock, 1, ItemId::CraftingTableBlock, 1},
        {Block::Stone, 1.5f, ToolClass::Pickaxe, ToolTier::Wood, ItemId::CobblestoneBlock, 1, ItemId::None, 0},
        {Block::Cobblestone, 2.0f, ToolClass::Pickaxe, ToolTier::Wood, ItemId::CobblestoneBlock, 1, ItemId::None, 0},
        {Block::CoalOre, 3.0f, ToolClass::Pickaxe, ToolTier::Wood, ItemId::Coal, 1, ItemId::None, 0},
        {Block::IronOre, 3.0f, ToolClass::Pickaxe, ToolTier::Stone, ItemId::RawIron, 1, ItemId::None, 0},
        {Block::DiamondOre, 3.0f, ToolClass::Pickaxe, ToolTier::Iron, ItemId::Diamond, 1, ItemId::None, 0},
        {Block::Furnace, 3.5f, ToolClass::Pickaxe, ToolTier::Wood, ItemId::FurnaceBlock, 1, ItemId::None, 0},
        {Block::Bedrock, UNBREAKABLE, ToolClass::None, ToolTier::Hand, ItemId::None, 0, ItemId::None, 0},
        {Block::Water, 0.0f, ToolClass::None, ToolTier::Hand, ItemId::None, 0, ItemId::None, 0},
    };
    for (const ExpectedHarvestRow& row : rows) {
        const BlockDef& d = blockDef(row.block);
        CHECK(std::fabs(d.hardness - row.hardness) < 0.001f);
        CHECK(d.preferredTool == row.preferredTool);
        CHECK(d.minHarvestTier == row.minTier);
        CHECK(d.dropItem == row.drop);
        CHECK(d.dropCount == row.dropCount);
        CHECK(d.wrongToolDropItem == row.wrongDrop);
        CHECK(d.wrongToolDropCount == row.wrongDropCount);
    }
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

    // Stacked water culls the water-water pair, and the uniformly-lit side
    // columns greedy-merge: 4 sides + top + bottom = 6 quads.
    s.blocks[at(8, 39, 8)] = Block::Water;
    md = buildMeshData(s);
    CHECK(md.inds.empty());
    CHECK(md.waterInds.size() == 6u * 6);
}

static void testTorchMesh() {
    // A torch on stone: custom post geometry, bottom face culled against the
    // opaque ground -> 5 quads, all in the opaque mesh, never greedy-merged.
    ChunkSnapshot s;
    s.blocks.assign(Chunk::rawSize(), Block::Air);
    auto at = [](int x, int y, int z) { return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x; };
    s.blocks[at(8, 39, 8)] = Block::Stone;
    s.blocks[at(8, 40, 8)] = Block::Torch;
    MeshData md = buildMeshData(s);
    CHECK(md.inds.size() == (6u + 5u) * 6); // stone box + 5 torch faces
    // The post is 2/16 wide centred in the cell and 10/16 tall.
    bool postSeen = false;
    for (const ChunkVertex& v : md.verts)
        if (v.layer == 9) {
            postSeen = true;
            CHECK(v.x >= 8 * 16 + 7 && v.x <= 8 * 16 + 9);
            CHECK(v.y >= 40 * 16 && v.y <= 40 * 16 + 10);
        }
    CHECK(postSeen);
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

struct FakeLightingAccess : lighting::Accessor {
    static constexpr int R = 3;
    static constexpr int W = R * 2 + 1;
    std::vector<Block> blocks;
    std::vector<uint8_t> sun;
    std::vector<uint8_t> block;
    int changed = 0;

    FakeLightingAccess()
        : blocks(size_t(W) * CHUNK_HEIGHT * W, Block::Air),
          sun(blocks.size(), 0), block(blocks.size(), 0) {}

    bool hasChunkAt(int wx, int wz) const override {
        return wx >= -R && wx <= R && wz >= -R && wz <= R;
    }
    Block blockAt(int wx, int wy, int wz) const override {
        if (!hasChunkAt(wx, wz) || wy < 0 || wy >= CHUNK_HEIGHT) return Block::Air;
        return blocks[index(wx, wy, wz)];
    }
    uint8_t cellLightAt(lighting::Channel ch, int wx, int wy, int wz) const override {
        const std::vector<uint8_t>& cells = ch == lighting::Channel::Sun ? sun : block;
        return cells[index(wx, wy, wz)];
    }
    void setCellLight(lighting::Channel ch, int wx, int wy, int wz, uint8_t v) override {
        std::vector<uint8_t>& cells = ch == lighting::Channel::Sun ? sun : block;
        cells[index(wx, wy, wz)] = v;
        ++changed;
    }
    void setBlock(int wx, int wy, int wz, Block b) {
        blocks[index(wx, wy, wz)] = b;
    }

private:
    static size_t index(int wx, int wy, int wz) {
        return size_t((wy * W + (wz + R)) * W + (wx + R));
    }
};

static void testLightingAccessorTorchRelight() {
    FakeLightingAccess a;
    glm::ivec3 p(0, 20, 0);

    a.setBlock(p.x, p.y, p.z, Block::Torch);
    lighting::onBlockChanged(a, Block::Air, Block::Torch, p);
    CHECK(lighting::lightAt(a, lighting::Channel::Block, p.x, p.y, p.z) == 14);
    CHECK(lighting::lightAt(a, lighting::Channel::Block, p.x + 1, p.y, p.z) == 13);
    CHECK(a.changed > 0);

    a.setBlock(p.x, p.y, p.z, Block::Air);
    lighting::onBlockChanged(a, Block::Torch, Block::Air, p);
    CHECK(lighting::lightAt(a, lighting::Channel::Block, p.x, p.y, p.z) == 0);
    CHECK(lighting::lightAt(a, lighting::Channel::Block, p.x + 1, p.y, p.z) == 0);
}

// Body physics on a hand-built platform high above terrain (y=70 is air
// everywhere near spawn: terrain tops out ~45 with trees).
static void testBodyPhysics() {
    std::filesystem::remove_all("test_phys_save");
    World w(1337, "test_phys_save");
    w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 1, 10000);
    w.setBlock(0, 70, 0, Block::Stone);

    // Falls and lands exactly on top of the platform.
    Body b;
    b.halfWidth = 0.125f;
    b.height = 0.25f;
    b.pos = glm::vec3(0.5f, 75.0f, 0.5f);
    for (int i = 0; i < 200 && !b.onGround; ++i) {
        b.vel.y += -22.0f * 0.05f;
        moveBody(w, b, 0.05f);
    }
    CHECK(b.onGround);
    CHECK(std::abs(b.pos.y - 71.0f) < 1e-3f);
    CHECK(b.vel.y == 0.0f);

    // Horizontal motion stops at a wall and zeroes that velocity component.
    w.setBlock(1, 71, 0, Block::Stone);
    b.vel = glm::vec3(4.0f, 0.0f, 0.0f);
    for (int i = 0; i < 40; ++i) moveBody(w, b, 0.05f);
    CHECK(b.pos.x <= 1.0f - 0.125f + 1e-4f);
    CHECK(b.vel.x == 0.0f);

    std::filesystem::remove_all("test_phys_save");
}

static void testMoveBodyAxisFlushBoundaries() {
    std::filesystem::remove_all("test_phys_flush_save");
    World w(1337, "test_phys_flush_save");
    w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 1, 10000);

    struct Case {
        glm::vec3 pos;
        glm::vec3 vel;
        glm::ivec3 obstacle;
        int axis;
        float amount;
        float expected;
        bool expectGround;
    };
    const Case cases[] = {
        {{0.62f, 70.0f, 0.5f}, {4.0f, 0.0f, 0.0f}, {1, 70, 0}, 0, 0.20f, 0.70f, false},
        {{0.32f, 70.0f, 0.5f}, {-4.0f, 0.0f, 0.0f}, {-1, 70, 0}, 0, -0.20f, 0.30f, false},
        {{0.5f, 70.0f, 0.62f}, {0.0f, 0.0f, 4.0f}, {0, 70, 1}, 2, 0.20f, 0.70f, false},
        {{0.5f, 70.0f, 0.32f}, {0.0f, 0.0f, -4.0f}, {0, 70, -1}, 2, -0.20f, 0.30f, false},
        {{0.70f, 70.0f, 0.5f}, {-4.0f, 0.0f, 0.0f}, {-1, 70, 0}, 0, -1.40f, 0.30f, false},
        {{0.5f, 70.20f, 0.5f}, {0.0f, -6.0f, 0.0f}, {0, 69, 0}, 1, -0.30f, 70.0f, true},
        {{0.5f, 70.0f, 0.5f}, {0.0f, 3.0f, 0.0f}, {0, 72, 0}, 1, 0.30f, 70.20f, false},
    };
    for (const Case& tc : cases) {
        w.setBlock(tc.obstacle.x, tc.obstacle.y, tc.obstacle.z, Block::Stone);
        Body b;
        b.pos = tc.pos;
        b.vel = tc.vel;
        b.halfWidth = 0.3f;
        b.height = 1.8f;
        CHECK(!bodyCollidesAt(w, b, b.pos));
        moveBodyAxis(w, b, tc.axis, tc.amount);
        CHECK(std::fabs(b.pos[tc.axis] - tc.expected) < 1e-4f);
        CHECK(b.onGround == tc.expectGround);
        CHECK(!bodyCollidesAt(w, b, b.pos));
        w.setBlock(tc.obstacle.x, tc.obstacle.y, tc.obstacle.z, Block::Air);
    }

    std::filesystem::remove_all("test_phys_flush_save");
}

static void testPlayerOwnsCanonicalBody() {
    Player p;
    p.body().pos = glm::vec3(2.0f, 3.0f, 4.0f);
    p.body().vel = glm::vec3(0.5f, -1.0f, 1.5f);
    p.body().onGround = true;
    CHECK(&p.pos() == &p.body().pos);
    CHECK(&p.vel() == &p.body().vel);
    CHECK(&p.onGround() == &p.body().onGround);
    CHECK(p.pos() == glm::vec3(2.0f, 3.0f, 4.0f));
    CHECK(p.vel() == glm::vec3(0.5f, -1.0f, 1.5f));
    CHECK(p.onGround());
}

// The Player refactor onto Body must not change player physics.
static void testPlayerLandsOnPlatform() {
    std::filesystem::remove_all("test_player_save");
    World w(1337, "test_player_save");
    w.waitUntilLoaded(glm::vec3(8.5f, 50.0f, 8.5f), 1, 10000);
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz)
            w.setBlock(8 + dx, 70, 8 + dz, Block::Stone);
    Player p;
    p.pos() = glm::vec3(9.0f, 75.0f, 9.0f);
    PlayerInput in;
    for (int i = 0; i < 200 && !p.onGround(); ++i) p.update(w, in, 0.05f);
    CHECK(p.onGround());
    CHECK(std::abs(p.pos().y - 71.0f) < 1e-3f);
    std::filesystem::remove_all("test_player_save");
}

static void testInventory() {
    Inventory inv;
    // Fills hotbar-first, stacks to 64, overflows into the next slot.
    CHECK(inv.add(ItemId::DirtBlock, 70) == 0);
    CHECK(inv.slots[0].item == ItemId::DirtBlock && inv.slots[0].count == 64);
    CHECK(inv.slots[1].item == ItemId::DirtBlock && inv.slots[1].count == 6);
    // Tops up existing stacks before opening a new one.
    CHECK(inv.add(ItemId::DirtBlock, 58) == 0);
    CHECK(inv.slots[1].count == 64 && inv.slots[2].empty());
    // Different block goes to the first empty slot.
    CHECK(inv.add(ItemId::StoneBlock, 1) == 0);
    CHECK(inv.slots[2].item == ItemId::StoneBlock && inv.slots[2].count == 1);
    // consumeOne decrements and empties.
    CHECK(inv.consumeOne(2));
    CHECK(inv.slots[2].empty());
    CHECK(!inv.consumeOne(2));
    // Tools are durable, non-stackable, and use separate slots.
    CHECK(inv.add(ItemId::WoodPickaxe, 2) == 0);
    CHECK(inv.slots[2].item == ItemId::WoodPickaxe && inv.slots[2].count == 1);
    CHECK(inv.slots[2].durability == itemDef(ItemId::WoodPickaxe).maxDurability);
    CHECK(inv.slots[3].item == ItemId::WoodPickaxe && inv.slots[3].count == 1);
    // Full inventory reports leftover.
    Inventory full;
    for (int i = 0; i < Inventory::SLOTS; ++i) CHECK(full.add(ItemId::StoneBlock, 64) == 0);
    CHECK(full.add(ItemId::StoneBlock, 10) == 10);

    // Invalid ids are rejected instead of being stored with clamped metadata.
    CHECK(inv.add(ItemId(65000), 5) == 5);
    for (int i = 0; i < Inventory::SLOTS; ++i) CHECK(inv.slots[i].item != ItemId(65000));

    Inventory split;
    CHECK(split.addStack({ItemId::Coal, 130, 0}) == 0);
    CHECK(split.slots[0].item == ItemId::Coal && split.slots[0].count == 64);
    CHECK(split.slots[1].item == ItemId::Coal && split.slots[1].count == 64);
    CHECK(split.slots[2].item == ItemId::Coal && split.slots[2].count == 2);
}

static void testItemRegistry() {
    for (int i = 0; i < ITEM_TYPES; ++i) {
        ItemId id = ItemId(i);
        const ItemDef& d = itemDef(id);
        CHECK(d.name != nullptr && d.name[0] != '\0');
        CHECK(d.stackMax >= 1 && d.stackMax <= Inventory::STACK_MAX);
        if (d.maxDurability > 0) CHECK(d.stackMax == 1);
        if (d.toolClass != ToolClass::None) {
            CHECK(d.toolTier != ToolTier::Hand);
            CHECK(d.miningSpeed > 1.0f);
            CHECK(d.maxDurability > 0);
        }
        if (d.placeBlock != Block::Air) CHECK(itemForBlock(d.placeBlock) == id);
    }
    CHECK(itemDef(ItemId::Coal).fuelTicks == 1600);
    CHECK(itemDef(ItemId::WoodPickaxe).toolClass == ToolClass::Pickaxe);
    CHECK(itemDef(ItemId::WoodPickaxe).toolTier == ToolTier::Wood);
    CHECK(itemDef(ItemId::WoodPickaxe).maxDurability == 59);
    CHECK(itemDef(ItemId::DiamondShovel).miningSpeed == 8.0f);
    CHECK(itemDef(ItemId::DiamondShovel).maxDurability == 1561);
    CHECK(itemDef(ItemId::CobblestoneBlock).stackMax == 64);
    CHECK(itemDef(ItemId::PlanksBlock).stackMax == 64);
    CHECK(itemForBlock(Block::Dirt) == ItemId::DirtBlock);
    CHECK(itemForBlock(Block::Cobblestone) == ItemId::CobblestoneBlock);
    CHECK(itemForBlock(Block::Planks) == ItemId::PlanksBlock);
    CHECK(itemForBlock(Block::CraftingTable) == ItemId::CraftingTableBlock);
    CHECK(itemForBlock(Block::Furnace) == ItemId::FurnaceBlock);
    CHECK(itemForBlock(Block::DiamondOre) == ItemId::DiamondOreBlock);
    CHECK(placeBlockForItem(ItemId::CobblestoneBlock) == Block::Cobblestone);
    CHECK(placeBlockForItem(ItemId::TorchBlock) == Block::Torch);
    CHECK(placeBlockForItem(ItemId::Coal) == Block::Air);

    ItemStack a = makeToolStack(ItemId::StoneAxe);
    ItemStack b = makeToolStack(ItemId::StoneAxe);
    CHECK(a.durability == itemDef(ItemId::StoneAxe).maxDurability);
    CHECK(stacksCompatible(a, b)); // compatible, but stackMax 1 prevents merging
    --b.durability;
    CHECK(!stacksCompatible(a, b));
    CHECK(stacksCompatible({ItemId::Coal, 3, 0}, {ItemId::Coal, 4, 0}));
    CHECK(makeItemStack(ItemId(65000), 1).empty());
    CHECK(makeItemStack(ItemId::Coal, 999).count == 64);
    CHECK(makeItemStack(ItemId::WoodPickaxe, 99).count == 1);
}

static void testMiningRequiredTicksAndDrops() {
    using namespace mining;
    ItemStack hand;
    ItemStack woodPick = makeToolStack(ItemId::WoodPickaxe);
    ItemStack stonePick = makeToolStack(ItemId::StonePickaxe);
    ItemStack ironPick = makeToolStack(ItemId::IronPickaxe);
    ItemStack woodAxe = makeToolStack(ItemId::WoodAxe);
    ItemStack diamondShovel = makeToolStack(ItemId::DiamondShovel);

    CHECK(requiredBreakTicks(Block::Dirt, miningToolForStack(hand)) == 15);
    CHECK(canHarvestUsefulDrop(Block::Dirt, miningToolForStack(hand)));
    CHECK(miningDrop(Block::Dirt, miningToolForStack(hand)).item == ItemId::DirtBlock);
    CHECK(requiredBreakTicks(Block::Dirt, miningToolForStack(diamondShovel)) == 2);
    CHECK(shouldUseDurabilityForBreak(Block::Dirt, miningToolForStack(diamondShovel)));
    CHECK(requiredBreakTicks(Block::Wood, miningToolForStack(hand)) == 60);
    CHECK(canHarvestUsefulDrop(Block::Wood, miningToolForStack(hand)));
    CHECK(miningDrop(Block::Wood, miningToolForStack(hand)).item == ItemId::LogBlock);
    CHECK(requiredBreakTicks(Block::Wood, miningToolForStack(woodAxe)) == 30);
    CHECK(miningDrop(Block::CraftingTable, miningToolForStack(hand)).item == ItemId::CraftingTableBlock);

    CHECK(requiredBreakTicks(Block::Stone, miningToolForStack(hand)) == 150);
    CHECK(!canHarvestUsefulDrop(Block::Stone, miningToolForStack(hand)));
    CHECK(miningDrop(Block::Stone, miningToolForStack(hand)).empty());
    CHECK(requiredBreakTicks(Block::Stone, miningToolForStack(woodPick)) == 23);
    CHECK(canHarvestUsefulDrop(Block::Stone, miningToolForStack(woodPick)));
    CHECK(miningDrop(Block::Stone, miningToolForStack(woodPick)).item == ItemId::CobblestoneBlock);

    CHECK(requiredBreakTicks(Block::Cobblestone, miningToolForStack(stonePick)) == 15);
    CHECK(requiredBreakTicks(Block::CoalOre, miningToolForStack(hand)) == 300);
    CHECK(miningDrop(Block::CoalOre, miningToolForStack(hand)).empty());
    CHECK(requiredBreakTicks(Block::CoalOre, miningToolForStack(woodPick)) == 45);
    CHECK(miningDrop(Block::CoalOre, miningToolForStack(woodPick)).item == ItemId::Coal);
    CHECK(requiredBreakTicks(Block::IronOre, miningToolForStack(woodPick)) == 300);
    CHECK(miningDrop(Block::IronOre, miningToolForStack(woodPick)).empty());
    CHECK(requiredBreakTicks(Block::IronOre, miningToolForStack(stonePick)) == 23);
    CHECK(miningDrop(Block::IronOre, miningToolForStack(stonePick)).item == ItemId::RawIron);
    CHECK(requiredBreakTicks(Block::DiamondOre, miningToolForStack(stonePick)) == 300);
    CHECK(miningDrop(Block::DiamondOre, miningToolForStack(stonePick)).empty());
    CHECK(requiredBreakTicks(Block::DiamondOre, miningToolForStack(ironPick)) == 15);
    CHECK(miningDrop(Block::DiamondOre, miningToolForStack(ironPick)).item == ItemId::Diamond);
    CHECK(requiredBreakTicks(Block::Furnace, miningToolForStack(hand)) == 350);
    CHECK(miningDrop(Block::Furnace, miningToolForStack(hand)).empty());
    CHECK(requiredBreakTicks(Block::Furnace, miningToolForStack(woodPick)) == 53);
    CHECK(miningDrop(Block::Furnace, miningToolForStack(woodPick)).item == ItemId::FurnaceBlock);

    CHECK(requiredBreakTicks(Block::Bedrock, miningToolForStack(ironPick)) == NEVER_BREAKS);
    CHECK(requiredBreakTicks(Block::Torch, miningToolForStack(hand)) == INSTANT_BREAK);
    CHECK(!shouldUseDurabilityForBreak(Block::Torch, miningToolForStack(ironPick)));
    CHECK(miningDrop(Block::Torch, miningToolForStack(hand)).item == ItemId::TorchBlock);
}

static void testMiningDurabilityUse() {
    using namespace mining;
    ItemStack hand;
    CHECK(applyDurabilityUse(hand, DurabilityUseReason::Mining));
    CHECK(hand.empty());

    ItemStack pick = makeToolStack(ItemId::WoodPickaxe);
    uint16_t start = pick.durability;
    CHECK(applyDurabilityUse(pick, DurabilityUseReason::Mining));
    CHECK(pick.item == ItemId::WoodPickaxe && pick.durability == start - 1);

    pick.durability = 1;
    CHECK(!applyDurabilityUse(pick, DurabilityUseReason::Mining));
    CHECK(pick.empty());
}

static void testMiningProgressResetsAndBreakEvent() {
    using namespace mining;
    BreakProgressState state;
    ItemStack hand;
    glm::ivec3 a(1, 2, 3);
    glm::ivec3 b(1, 2, 4);

    for (int i = 0; i < 14; ++i) {
        BreakProgressEvent ev = advanceBreakProgress(state, true, true, a, Block::Dirt, hand);
        CHECK(!ev.removed);
        CHECK(!ev.useDurability);
        CHECK(state.active);
    }
    BreakProgressEvent ev = advanceBreakProgress(state, true, true, a, Block::Dirt, hand);
    CHECK(ev.removed);
    CHECK(!ev.useDurability);
    CHECK(!state.active);

    ItemStack shovel = makeToolStack(ItemId::WoodShovel);
    ev = advanceBreakProgress(state, true, true, a, Block::Dirt, shovel);
    CHECK(!ev.removed && ev.progress > 0.0f);
    for (int i = 0; i < 7; ++i)
        ev = advanceBreakProgress(state, true, true, a, Block::Dirt, shovel);
    CHECK(ev.removed);
    CHECK(ev.useDurability);

    advanceBreakProgress(state, true, true, a, Block::Wood, hand);
    CHECK(state.active && state.ticks == 1);
    advanceBreakProgress(state, true, true, a, Block::Wood, hand);
    CHECK(state.ticks == 2);
    ev = advanceBreakProgress(state, true, true, b, Block::Wood, hand);
    CHECK(ev.reset);
    CHECK(state.active && state.target == b && state.ticks == 1);
    ev = advanceBreakProgress(state, false, true, b, Block::Wood, hand);
    CHECK(ev.reset);
    CHECK(!state.active && state.ticks == 0);
    advanceBreakProgress(state, true, true, a, Block::Wood, hand);
    ev = advanceBreakProgress(state, true, true, a, Block::Wood,
                              makeItemStack(ItemId::Coal, 1));
    CHECK(ev.reset);
    CHECK(state.active && state.ticks == 1);
    ev = advanceBreakProgress(state, true, false, a, Block::Wood, hand);
    CHECK(ev.reset);
    CHECK(!state.active);

    ev = advanceBreakProgress(state, true, true, a, Block::Bedrock, hand);
    CHECK(!ev.removed);
    CHECK(!state.active);
    ev = advanceBreakProgress(state, true, true, a, Block::Torch, hand);
    CHECK(ev.removed);
    CHECK(!ev.useDurability);
    CHECK(!state.active);
}

static crafting::CraftingGrid grid(int size) {
    crafting::CraftingGrid g;
    g.width = size;
    g.height = size;
    return g;
}

static void testCraftingRecipeMatching() {
    using namespace crafting;
    CHECK(recipeCount() == 17);

    CraftingGrid g2 = grid(2);
    g2.at(1, 1) = makeItemStack(ItemId::LogBlock, 1);
    CHECK(craftingOutput(g2).item == ItemId::PlanksBlock);
    CHECK(craftingOutput(g2).count == 4);

    CraftingGrid g3 = grid(3);
    g3.at(2, 0) = makeItemStack(ItemId::LogBlock, 1);
    CHECK(craftingOutput(g3).item == ItemId::PlanksBlock);

    g2 = grid(2);
    g2.at(0, 0) = makeItemStack(ItemId::PlanksBlock, 1);
    g2.at(0, 1) = makeItemStack(ItemId::PlanksBlock, 1);
    CHECK(craftingOutput(g2).item == ItemId::Stick);
    CHECK(craftingOutput(g2).count == 4);

    g2 = grid(2);
    g2.at(0, 0) = makeItemStack(ItemId::PlanksBlock, 1);
    g2.at(1, 0) = makeItemStack(ItemId::PlanksBlock, 1);
    g2.at(0, 1) = makeItemStack(ItemId::PlanksBlock, 1);
    g2.at(1, 1) = makeItemStack(ItemId::PlanksBlock, 1);
    CHECK(craftingOutput(g2).item == ItemId::CraftingTableBlock);

    g2 = grid(2);
    g2.at(1, 0) = makeItemStack(ItemId::Coal, 1);
    g2.at(1, 1) = makeItemStack(ItemId::Stick, 1);
    CHECK(craftingOutput(g2).item == ItemId::TorchBlock);
    CHECK(craftingOutput(g2).count == 4);

    g3 = grid(3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            if (!(x == 1 && y == 1))
                g3.at(x, y) = makeItemStack(ItemId::CobblestoneBlock, 1);
    CHECK(craftingOutput(g3).item == ItemId::FurnaceBlock);

    g2 = grid(2);
    g2.at(0, 0) = makeItemStack(ItemId::CobblestoneBlock, 1);
    g2.at(1, 0) = makeItemStack(ItemId::CobblestoneBlock, 1);
    g2.at(0, 1) = makeItemStack(ItemId::CobblestoneBlock, 1);
    g2.at(1, 1) = makeItemStack(ItemId::CobblestoneBlock, 1);
    CHECK(craftingOutput(g2).empty());

    struct ToolCase { ItemId material, pickaxe, axe, shovel; };
    const ToolCase tools[] = {
        {ItemId::PlanksBlock, ItemId::WoodPickaxe, ItemId::WoodAxe, ItemId::WoodShovel},
        {ItemId::CobblestoneBlock, ItemId::StonePickaxe, ItemId::StoneAxe, ItemId::StoneShovel},
        {ItemId::IronIngot, ItemId::IronPickaxe, ItemId::IronAxe, ItemId::IronShovel},
        {ItemId::Diamond, ItemId::DiamondPickaxe, ItemId::DiamondAxe, ItemId::DiamondShovel},
    };
    for (const ToolCase& t : tools) {
        g3 = grid(3);
        for (int x = 0; x < 3; ++x) g3.at(x, 0) = makeItemStack(t.material, 1);
        g3.at(1, 1) = makeItemStack(ItemId::Stick, 1);
        g3.at(1, 2) = makeItemStack(ItemId::Stick, 1);
        ItemStack out = craftingOutput(g3);
        CHECK(out.item == t.pickaxe);
        CHECK(out.durability == itemDef(t.pickaxe).maxDurability);

        g3 = grid(3);
        g3.at(0, 0) = makeItemStack(t.material, 1);
        g3.at(1, 0) = makeItemStack(t.material, 1);
        g3.at(0, 1) = makeItemStack(t.material, 1);
        g3.at(1, 1) = makeItemStack(ItemId::Stick, 1);
        g3.at(1, 2) = makeItemStack(ItemId::Stick, 1);
        out = craftingOutput(g3);
        CHECK(out.item == t.axe);
        CHECK(out.durability == itemDef(t.axe).maxDurability);

        g3 = grid(3);
        g3.at(1, 0) = makeItemStack(t.material, 1);
        g3.at(1, 1) = makeItemStack(ItemId::Stick, 1);
        g3.at(1, 2) = makeItemStack(ItemId::Stick, 1);
        out = craftingOutput(g3);
        CHECK(out.item == t.shovel);
        CHECK(out.durability == itemDef(t.shovel).maxDurability);
    }

    g3 = grid(3);
    g3.at(0, 0) = makeItemStack(ItemId::PlanksBlock, 1);
    g3.at(2, 2) = makeItemStack(ItemId::Stick, 1);
    CHECK(craftingOutput(g3).empty());
}

static void testCraftingConsumptionAndCursorOutput() {
    using namespace crafting;
    CraftingGrid g = grid(2);
    g.at(0, 0) = makeItemStack(ItemId::LogBlock, 3);
    ItemStack cursor;
    CHECK(craftToCursor(g, cursor));
    CHECK(cursor.item == ItemId::PlanksBlock && cursor.count == 4);
    CHECK(g.at(0, 0).item == ItemId::LogBlock && g.at(0, 0).count == 2);

    CHECK(craftToCursor(g, cursor));
    CHECK(cursor.item == ItemId::PlanksBlock && cursor.count == 8);
    CHECK(g.at(0, 0).count == 1);

    cursor = makeItemStack(ItemId::Coal, 1);
    ItemStack beforeInput = g.at(0, 0);
    CHECK(!craftToCursor(g, cursor));
    CHECK(cursor.item == ItemId::Coal && cursor.count == 1);
    CHECK(g.at(0, 0).item == beforeInput.item && g.at(0, 0).count == beforeInput.count);

    CraftingGrid pick = grid(3);
    for (int x = 0; x < 3; ++x) pick.at(x, 0) = makeItemStack(ItemId::IronIngot, 2);
    pick.at(1, 1) = makeItemStack(ItemId::Stick, 2);
    pick.at(1, 2) = makeItemStack(ItemId::Stick, 2);
    cursor = {};
    CHECK(craftToCursor(pick, cursor));
    CHECK(cursor.item == ItemId::IronPickaxe && cursor.durability == itemDef(ItemId::IronPickaxe).maxDurability);
    CHECK(pick.at(0, 0).count == 1 && pick.at(1, 0).count == 1 && pick.at(2, 0).count == 1);
    CHECK(pick.at(1, 1).count == 1 && pick.at(1, 2).count == 1);
}

static void testFurnaceSmeltingTicksAndFuel() {
    BlockEntityStore store;
    FurnaceState& f = store.getOrCreateFurnace({1, 2, 3});
    f.input = makeItemStack(ItemId::RawIron, 8);
    f.fuel = makeItemStack(ItemId::Coal, 1);

    for (int i = 0; i < 199; ++i) store.tickFurnaces();
    CHECK(f.output.empty());
    CHECK(f.cookTicks == 199);
    CHECK(f.burnTicksRemaining == 1401);
    store.tickFurnaces();
    CHECK(f.output.item == ItemId::IronIngot && f.output.count == 1);
    CHECK(f.input.count == 7);
    CHECK(f.fuel.empty());
    CHECK(f.cookTicks == 0);
    CHECK(f.burnTicksRemaining == 1400);

    for (int i = 0; i < 1400; ++i) store.tickFurnaces();
    CHECK(f.output.item == ItemId::IronIngot && f.output.count == 8);
    CHECK(f.input.empty());
    CHECK(f.burnTicksRemaining == 0);
    CHECK(f.cookTicks == 0);
}

static void testFurnaceBlockedAndMissingInputBurns() {
    BlockEntityStore store;
    FurnaceState& blocked = store.getOrCreateFurnace({0, 1, 0});
    blocked.input = makeItemStack(ItemId::RawIron, 1);
    blocked.fuel = makeItemStack(ItemId::Coal, 1);
    blocked.output = makeItemStack(ItemId::IronIngot, 64);
    store.tickFurnaces();
    CHECK(blocked.fuel.item == ItemId::Coal && blocked.fuel.count == 1);
    CHECK(blocked.burnTicksRemaining == 0);
    CHECK(blocked.cookTicks == 0);
    CHECK(blocked.output.count == 64);
    blocked.burnTicksRemaining = 7;
    store.tickFurnaces();
    CHECK(blocked.burnTicksRemaining == 6);
    CHECK(blocked.fuel.item == ItemId::Coal && blocked.fuel.count == 1);
    CHECK(blocked.cookTicks == 0);
    CHECK(blocked.output.count == 64);

    FurnaceState& missing = store.getOrCreateFurnace({1, 1, 0});
    missing.fuel = makeItemStack(ItemId::Coal, 1);
    store.tickFurnaces();
    CHECK(missing.fuel.item == ItemId::Coal && missing.fuel.count == 1);
    CHECK(missing.burnTicksRemaining == 0);
    CHECK(missing.cookTicks == 0);
    missing.burnTicksRemaining = 5;
    store.tickFurnaces();
    CHECK(missing.fuel.item == ItemId::Coal && missing.fuel.count == 1);
    CHECK(missing.burnTicksRemaining == 4);
    CHECK(missing.cookTicks == 0);
}

static void testFurnaceSaveLoadAndRemoval() {
    std::filesystem::remove_all("test_block_entities");
    std::filesystem::create_directories("test_block_entities");
    std::string path = "test_block_entities/block_entities.bin";

    BlockEntityStore store;
    FurnaceState& f = store.getOrCreateFurnace({-4, 12, 9});
    f.input = makeItemStack(ItemId::RawIron, 3);
    f.fuel = makeItemStack(ItemId::Coal, 2);
    f.output = makeItemStack(ItemId::IronIngot, 5);
    f.burnTicksRemaining = 123;
    f.cookTicks = 45;
    CHECK(saveBlockEntitiesFile(path, store));

    BlockEntityStore loaded;
    CHECK(loadBlockEntitiesFile(path, loaded));
    FurnaceState* lf = loaded.furnaceAt({-4, 12, 9});
    CHECK(lf != nullptr);
    CHECK(lf->input.item == ItemId::RawIron && lf->input.count == 3);
    CHECK(lf->fuel.item == ItemId::Coal && lf->fuel.count == 2);
    CHECK(lf->output.item == ItemId::IronIngot && lf->output.count == 5);
    CHECK(lf->burnTicksRemaining == 123 && lf->cookTicks == 45);

    loaded.removeFurnace({-4, 12, 9});
    CHECK(loaded.furnaceAt({-4, 12, 9}) == nullptr);

    {
        std::ofstream bad(path, std::ios::binary | std::ios::trunc);
        bad.write("NOPE", 4);
    }
    BlockEntityStore badLoaded;
    badLoaded.getOrCreateFurnace({1, 2, 3}).output = makeItemStack(ItemId::IronIngot, 1);
    CHECK(!loadBlockEntitiesFile(path, badLoaded));
    CHECK(badLoaded.furnaceCount() == 0);

    std::filesystem::remove_all("test_block_entities");
}

static void testWorldOwnsFurnaceState() {
    std::filesystem::remove_all("test_world_be");
    {
        World w(1337, "test_world_be");
        w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 1, 10000);
        w.setBlock(0, 70, 0, Block::Furnace);
        FurnaceState& f = w.getOrCreateFurnace({0, 70, 0});
        f.input = makeItemStack(ItemId::RawIron, 1);
        f.fuel = makeItemStack(ItemId::Coal, 1);
        for (int i = 0; i < 200; ++i) w.tickBlockEntities();
        CHECK(f.output.item == ItemId::IronIngot && f.output.count == 1);
        w.saveAllModified();
    }
    {
        World w(1337, "test_world_be");
        FurnaceState* f = w.furnaceAt({0, 70, 0});
        CHECK(f != nullptr);
        CHECK(f->output.item == ItemId::IronIngot && f->output.count == 1);
        w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 1, 10000);
        w.setBlock(0, 70, 0, Block::Air);
        CHECK(w.furnaceAt({0, 70, 0}) == nullptr);
    }
    std::filesystem::remove_all("test_world_be");
}

static void testFurnaceBreakTakesContents() {
    std::filesystem::remove_all("test_world_furnace_break");
    World w(1337, "test_world_furnace_break");
    w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 1, 10000);
    glm::ivec3 pos(0, 70, 0);
    w.setBlock(pos.x, pos.y, pos.z, Block::Furnace);
    FurnaceState& f = w.getOrCreateFurnace(pos);
    f.input = makeItemStack(ItemId::RawIron, 3);
    f.fuel = makeItemStack(ItemId::Coal, 2);
    f.output = makeItemStack(ItemId::IronIngot, 1);

    CHECK(mining::miningDrop(Block::Furnace,
                             mining::miningToolForStack(makeToolStack(ItemId::WoodPickaxe))).item
          == ItemId::FurnaceBlock);
    std::vector<ItemStack> contents = w.takeFurnaceContents(pos);
    CHECK(contents.size() == 3);
    CHECK(contents[0].item == ItemId::RawIron && contents[0].count == 3);
    CHECK(contents[1].item == ItemId::Coal && contents[1].count == 2);
    CHECK(contents[2].item == ItemId::IronIngot && contents[2].count == 1);
    CHECK(w.furnaceAt(pos) == nullptr);
    CHECK(w.takeFurnaceContents(pos).empty());
    std::filesystem::remove_all("test_world_furnace_break");
}

static void testWrongToolFurnaceBreakStillTakesContents() {
    std::filesystem::remove_all("test_world_furnace_wrong");
    World w(1337, "test_world_furnace_wrong");
    w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 1, 10000);
    glm::ivec3 pos(0, 70, 0);
    w.setBlock(pos.x, pos.y, pos.z, Block::Furnace);
    FurnaceState& f = w.getOrCreateFurnace(pos);
    f.output = makeItemStack(ItemId::IronIngot, 4);

    CHECK(mining::miningDrop(Block::Furnace, mining::miningToolForStack(ItemStack{})).empty());
    std::vector<ItemStack> contents = w.takeFurnaceContents(pos);
    CHECK(contents.size() == 1);
    CHECK(contents[0].item == ItemId::IronIngot && contents[0].count == 4);
    CHECK(w.furnaceAt(pos) == nullptr);
    std::filesystem::remove_all("test_world_furnace_wrong");
}

static void testItemEntityFallsAndLands() {
    std::filesystem::remove_all("test_ent_save");
    World w(1337, "test_ent_save");
    w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 1, 10000);
    w.setBlock(0, 70, 0, Block::Stone);
    Entities ents;
    ents.spawnItem(glm::vec3(0.5f, 75.0f, 0.5f), glm::vec3(0.0f), ItemId::DirtBlock, 1);
    glm::vec3 farAway(500.0f, 50.0f, 500.0f); // out of magnet range
    for (int i = 0; i < 100; ++i) ents.tick(w, farAway, nullptr, 0.05f);
    CHECK(ents.items().size() == 1);
    const ItemEntity& e = *ents.items()[0];
    CHECK(e.body.onGround);
    CHECK(std::abs(e.body.pos.y - 71.0f) < 1e-3f);
    std::filesystem::remove_all("test_ent_save");
}

static void testItemPickup() {
    std::filesystem::remove_all("test_ent_save2");
    World w(1337, "test_ent_save2");
    w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 1, 10000);
    w.setBlock(0, 70, 0, Block::Stone);
    Entities ents;
    Inventory inv;
    ents.spawnItem(glm::vec3(0.5f, 71.0f, 0.5f), glm::vec3(0.0f), ItemId::DirtBlock, 1);
    glm::vec3 playerPos(1.5f, 71.0f, 0.5f); // one block away: in magnet range
    for (int i = 0; i < 60 && !ents.items().empty(); ++i)
        ents.tick(w, playerPos, &inv, 0.05f);
    CHECK(ents.items().empty()); // magnetized in and collected
    CHECK(inv.slots[0].item == ItemId::DirtBlock && inv.slots[0].count == 1);
    std::filesystem::remove_all("test_ent_save2");
}

static void testItemPickupPreservesDurabilityAndRemainder() {
    std::filesystem::remove_all("test_ent_save_pickup_remainder");
    World w(1337, "test_ent_save_pickup_remainder");
    w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 1, 10000);
    w.setBlock(0, 70, 0, Block::Stone);

    Entities tools;
    Inventory toolInv;
    ItemStack damaged = makeToolStack(ItemId::IronPickaxe);
    damaged.durability = 7;
    tools.spawnItem(glm::vec3(0.5f, 71.0f, 0.5f), glm::vec3(0.0f), damaged);
    for (int i = 0; i < 60 && !tools.items().empty(); ++i)
        tools.tick(w, glm::vec3(0.5f, 71.0f, 0.5f), &toolInv, 0.05f);
    CHECK(tools.items().empty());
    CHECK(toolInv.slots[0].item == ItemId::IronPickaxe);
    CHECK(toolInv.slots[0].durability == 7);

    Entities coal;
    Inventory almostFull;
    almostFull.slots[0] = makeItemStack(ItemId::Coal, 63);
    for (int i = 1; i < Inventory::SLOTS; ++i)
        almostFull.slots[i] = makeItemStack(ItemId::StoneBlock, 64);
    coal.spawnItem(glm::vec3(0.5f, 71.0f, 0.5f), glm::vec3(0.0f),
                   makeItemStack(ItemId::Coal, 5));
    for (int i = 0; i < 60; ++i)
        coal.tick(w, glm::vec3(0.5f, 71.0f, 0.5f), &almostFull, 0.05f);
    CHECK(almostFull.slots[0].count == 64);
    CHECK(coal.items().size() == 1);
    CHECK(coal.items()[0]->stack.item == ItemId::Coal);
    CHECK(coal.items()[0]->stack.count == 4);

    std::filesystem::remove_all("test_ent_save_pickup_remainder");
}

static void testItemFrozenInUnloadedChunk() {
    std::filesystem::remove_all("test_ent_save3");
    World w(1337, "test_ent_save3");
    w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 1, 10000);
    Entities ents;
    glm::vec3 farPos(1000.5f, 50.0f, 1000.5f); // chunk never loaded
    ents.spawnItem(farPos, glm::vec3(0.0f), ItemId::StoneBlock, 1);
    for (int i = 0; i < 20; ++i) ents.tick(w, glm::vec3(0.0f), nullptr, 0.05f);
    CHECK(ents.items()[0]->body.pos == farPos); // no physics in the void
    std::filesystem::remove_all("test_ent_save3");
}

static void testItemEntityMerging() {
    std::filesystem::remove_all("test_ent_merge");
    World w(1337, "test_ent_merge");
    w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 1, 10000);

    Entities stackMerge;
    stackMerge.spawnItem(glm::vec3(0.5f, 70.0f, 0.5f), glm::vec3(0.0f),
                         makeItemStack(ItemId::Coal, 30));
    stackMerge.spawnItem(glm::vec3(1.0f, 70.0f, 0.5f), glm::vec3(0.0f),
                         makeItemStack(ItemId::Coal, 40));
    stackMerge.tick(w, glm::vec3(500.0f, 50.0f, 500.0f), nullptr, 0.05f);
    CHECK(stackMerge.items().size() == 2);
    CHECK(stackMerge.items()[0]->stack.count == 64);
    CHECK(stackMerge.items()[1]->stack.count == 6);

    Entities atRadius;
    atRadius.spawnItem(glm::vec3(0.5f, 70.0f, 0.5f), glm::vec3(0.0f),
                       makeItemStack(ItemId::Coal, 1));
    atRadius.spawnItem(glm::vec3(1.25f, 70.0f, 0.5f), glm::vec3(0.0f),
                       makeItemStack(ItemId::Coal, 1));
    atRadius.tick(w, glm::vec3(500.0f, 50.0f, 500.0f), nullptr, 0.05f);
    CHECK(atRadius.items().size() == 1);
    CHECK(atRadius.items()[0]->stack.count == 2);

    Entities outsideRadius;
    outsideRadius.spawnItem(glm::vec3(0.5f, 70.0f, 0.5f), glm::vec3(0.0f),
                            makeItemStack(ItemId::Coal, 1));
    outsideRadius.spawnItem(glm::vec3(1.26f, 70.0f, 0.5f), glm::vec3(0.0f),
                            makeItemStack(ItemId::Coal, 1));
    outsideRadius.tick(w, glm::vec3(500.0f, 50.0f, 500.0f), nullptr, 0.05f);
    CHECK(outsideRadius.items().size() == 2);

    Entities tools;
    tools.spawnItem(glm::vec3(0.5f, 70.0f, 0.5f), glm::vec3(0.0f),
                    makeToolStack(ItemId::WoodPickaxe));
    tools.spawnItem(glm::vec3(1.0f, 70.0f, 0.5f), glm::vec3(0.0f),
                    makeToolStack(ItemId::WoodPickaxe));
    tools.tick(w, glm::vec3(500.0f, 50.0f, 500.0f), nullptr, 0.05f);
    CHECK(tools.items().size() == 2);

    Entities unloaded;
    glm::vec3 farPos(1000.5f, 70.0f, 1000.5f);
    unloaded.spawnItem(farPos, glm::vec3(0.0f), makeItemStack(ItemId::Coal, 1));
    unloaded.spawnItem(farPos + glm::vec3(0.25f, 0.0f, 0.0f), glm::vec3(0.0f),
                       makeItemStack(ItemId::Coal, 1));
    unloaded.tick(w, glm::vec3(500.0f, 50.0f, 500.0f), nullptr, 0.05f);
    CHECK(unloaded.items().size() == 2);

    std::filesystem::remove_all("test_ent_merge");
}

static void testEntityBucketsAndDrops() {
    std::filesystem::remove_all("test_ent_save4");
    World w(1337, "test_ent_save4");
    w.waitUntilLoaded(glm::vec3(0.5f, 50.0f, 0.5f), 2, 10000);
    Entities ents;
    // Registry-driven drops: Grass drops Dirt, Leaves drop nothing.
    ents.spawnBlockDrop(glm::ivec3(0, 70, 0), Block::Grass);
    ents.spawnBlockDrop(glm::ivec3(0, 70, 0), Block::Leaves);
    ents.spawnBlockDrop(glm::ivec3(0, 70, 0), Block::Stone);
    CHECK(ents.items().size() == 2);
    CHECK(ents.items()[0]->stack.item == ItemId::DirtBlock);
    CHECK(ents.items()[1]->stack.item == ItemId::CobblestoneBlock);
    // Bucket query: a second item two chunks away is not "near".
    ents.spawnItem(glm::vec3(40.5f, 70.0f, 0.5f), glm::vec3(0.0f), ItemId::StoneBlock, 1);
    ents.tick(w, glm::vec3(500.0f, 50.0f, 500.0f), nullptr, 0.05f);
    CHECK(ents.itemsNear(glm::vec3(0.5f, 70.0f, 0.5f), 8.0f).size() == 2);
    CHECK(ents.itemsNear(glm::vec3(40.5f, 70.0f, 0.5f), 8.0f).size() == 1);
    std::filesystem::remove_all("test_ent_save4");
}

static void testNonBlockItemIconMapping() {
    for (int i = 1; i < ITEM_TYPES; ++i) {
        ItemId id = ItemId(i);
        bool blockItem = placeBlockForItem(id) != Block::Air;
        if (blockItem) {
            CHECK(itemUsesBlockCube(id));
        } else {
            CHECK(!itemUsesBlockCube(id));
            CHECK(itemIconTile(id) != TileId::Error);
            CHECK(int(itemIconTile(id)) < ATLAS_TILES);
        }
    }
    CHECK(itemIconTile(ItemId::Coal) != itemIconTile(ItemId::IronIngot));
    CHECK(itemIconTile(ItemId::WoodPickaxe) != itemIconTile(ItemId::WoodAxe));
}

static void testBreakOverlayHelpers() {
    static_assert(BREAK_CRACK_STAGES == 10, "break overlay uses 10 crack stages");
    CHECK(breakStageForProgress(-1.0f) == 0);
    CHECK(breakStageForProgress(0.0f) == 0);
    CHECK(breakStageForProgress(0.099f) == 0);
    CHECK(breakStageForProgress(0.10f) == 1);
    CHECK(breakStageForProgress(0.55f) == 5);
    CHECK(breakStageForProgress(0.999f) == 9);
    CHECK(breakStageForProgress(1.0f) == 9);
    CHECK(breakStageForProgress(5.0f) == 9);

    CHECK(breakFaceForAdjacent({4, 5, 6}, {5, 5, 6}) == 0);
    CHECK(breakFaceForAdjacent({4, 5, 6}, {3, 5, 6}) == 1);
    CHECK(breakFaceForAdjacent({4, 5, 6}, {4, 6, 6}) == 2);
    CHECK(breakFaceForAdjacent({4, 5, 6}, {4, 4, 6}) == 3);
    CHECK(breakFaceForAdjacent({4, 5, 6}, {4, 5, 7}) == 4);
    CHECK(breakFaceForAdjacent({4, 5, 6}, {4, 5, 5}) == 5);
    CHECK(breakFaceForAdjacent({4, 5, 6}, {4, 5, 6}) == -1);
}

static void testItemEntityStackIngress() {
    Entities ents;
    ents.spawnItem(glm::vec3(0.0f), glm::vec3(0.0f), ItemId(65000), 3);
    ents.spawnItem(glm::vec3(0.0f), glm::vec3(0.0f), ItemId::Coal, -1);
    CHECK(ents.items().empty());
    ents.spawnItem(glm::vec3(0.0f), glm::vec3(0.0f), ItemId::Coal, 130);
    CHECK(ents.items().size() == 3);
    CHECK(ents.items()[0]->stack.count == 64);
    CHECK(ents.items()[1]->stack.count == 64);
    CHECK(ents.items()[2]->stack.count == 2);
}

static void testPlayerSaveV3Roundtrip() {
    std::filesystem::create_directories("test_psave");
    PlayerState a;
    a.pos = glm::vec3(12.5f, 34.0f, -8.25f);
    a.yaw = 123.0f;
    a.pitch = -45.0f;
    a.flying = true;
    a.hotbarSlot = 3;
    a.inv.add(ItemId::DirtBlock, 70);
    a.inv.add(ItemId::TorchBlock, 5);
    a.inv.slots[4] = makeToolStack(ItemId::IronPickaxe);
    a.inv.slots[4].durability = 123;
    a.inv.add(ItemId::Coal, 9);
    CHECK(savePlayerFile("test_psave/player.bin", a));
    PlayerState b;
    CHECK(loadPlayerFile("test_psave/player.bin", b));
    CHECK(b.pos == a.pos && b.yaw == a.yaw && b.pitch == a.pitch);
    CHECK(b.flying && b.hotbarSlot == 3);
    for (int i = 0; i < Inventory::SLOTS; ++i) {
        CHECK(b.inv.slots[i].item == a.inv.slots[i].item);
        CHECK(b.inv.slots[i].count == a.inv.slots[i].count);
        CHECK(b.inv.slots[i].durability == a.inv.slots[i].durability);
    }
    std::filesystem::remove_all("test_psave");
}

static void testPlayerSaveV2BlockInventoryMigrates() {
    std::filesystem::create_directories("test_psave2");
    {
        std::ofstream f("test_psave2/player.bin", std::ios::binary);
        f.write("MCPL", 4);
        uint32_t v = 2;
        f.write(reinterpret_cast<const char*>(&v), 4);
        glm::vec3 pos(4.0f, 5.0f, 6.0f);
        float yaw = 30.0f, pitch = -10.0f;
        f.write(reinterpret_cast<const char*>(&pos), sizeof(pos));
        f.write(reinterpret_cast<const char*>(&yaw), 4);
        f.write(reinterpret_cast<const char*>(&pitch), 4);
        uint8_t flying = 0, slot = 5;
        f.write(reinterpret_cast<const char*>(&flying), 1);
        f.write(reinterpret_cast<const char*>(&slot), 1);
        for (int i = 0; i < Inventory::SLOTS; ++i) {
            uint8_t b = 0, c = 0;
            if (i == 0) { b = uint8_t(Block::Stone); c = 3; }
            if (i == 1) { b = uint8_t(Block::Dirt); c = 250; }
            if (i == 2) { b = uint8_t(Block::CoalOre); c = 1; }
            if (i == 3) { b = 250; c = 9; }
            f.write(reinterpret_cast<const char*>(&b), 1);
            f.write(reinterpret_cast<const char*>(&c), 1);
        }
    }
    PlayerState s;
    CHECK(loadPlayerFile("test_psave2/player.bin", s));
    CHECK(s.pos == glm::vec3(4.0f, 5.0f, 6.0f));
    CHECK(s.hotbarSlot == 5);
    CHECK(s.inv.slots[0].item == ItemId::CobblestoneBlock && s.inv.slots[0].count == 3);
    CHECK(s.inv.slots[1].item == ItemId::DirtBlock && s.inv.slots[1].count == 64);
    CHECK(s.inv.slots[2].item == ItemId::CoalOreBlock && s.inv.slots[2].count == 1);
    CHECK(s.inv.slots[3].empty());
    std::filesystem::remove_all("test_psave2");
}

static void testPlayerSaveV3UnknownItemSanitizes() {
    std::filesystem::create_directories("test_psave3");
    {
        std::ofstream f("test_psave3/player.bin", std::ios::binary);
        f.write("MCPL", 4);
        uint32_t v = PLAYER_VERSION;
        f.write(reinterpret_cast<const char*>(&v), 4);
        PlayerState base;
        f.write(reinterpret_cast<const char*>(&base.pos), sizeof(base.pos));
        f.write(reinterpret_cast<const char*>(&base.yaw), 4);
        f.write(reinterpret_cast<const char*>(&base.pitch), 4);
        uint8_t flying = 0, slot = 0;
        f.write(reinterpret_cast<const char*>(&flying), 1);
        f.write(reinterpret_cast<const char*>(&slot), 1);
        for (int i = 0; i < Inventory::SLOTS; ++i) {
            uint16_t id = i == 0 ? uint16_t(65000) : 0;
            uint8_t count = i == 0 ? 5 : 0;
            uint16_t durability = 0;
            f.write(reinterpret_cast<const char*>(&id), 2);
            f.write(reinterpret_cast<const char*>(&count), 1);
            f.write(reinterpret_cast<const char*>(&durability), 2);
        }
    }
    PlayerState s;
    CHECK(loadPlayerFile("test_psave3/player.bin", s));
    CHECK(s.inv.slots[0].empty());
    std::filesystem::remove_all("test_psave3");
}

static void testPlayerSaveV1Migrates() {
    // Hand-craft a v1 file: header + pos + yaw + pitch + flying + slot.
    std::filesystem::create_directories("test_psave1");
    {
        std::ofstream f("test_psave1/player.bin", std::ios::binary);
        f.write("MCPL", 4);
        uint32_t v = 1;
        f.write(reinterpret_cast<const char*>(&v), 4);
        glm::vec3 pos(1.0f, 2.0f, 3.0f);
        float yaw = 10.0f, pitch = 20.0f;
        f.write(reinterpret_cast<const char*>(&pos), sizeof(pos));
        f.write(reinterpret_cast<const char*>(&yaw), 4);
        f.write(reinterpret_cast<const char*>(&pitch), 4);
        uint8_t flying = 1, slot = 2;
        f.write(reinterpret_cast<const char*>(&flying), 1);
        f.write(reinterpret_cast<const char*>(&slot), 1);
    }
    PlayerState s;
    CHECK(loadPlayerFile("test_psave1/player.bin", s));
    CHECK(s.pos == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(s.flying && s.hotbarSlot == 2);
    for (int i = 0; i < Inventory::SLOTS; ++i) CHECK(s.inv.slots[i].empty());
    // Bad magic is rejected.
    {
        std::ofstream f("test_psave1/bad.bin", std::ios::binary);
        f.write("XXXX", 4);
    }
    CHECK(!loadPlayerFile("test_psave1/bad.bin", s));
    std::filesystem::remove_all("test_psave1");
}

static void testDayCycle() {
    // Day at the start, night at u=0.7, smooth and continuous in between.
    CHECK(sunLevelAt(0.0f) == 1.0f);
    CHECK(std::fabs(sunLevelAt(0.7f * DAY_LENGTH) - NIGHT_SUN) < 1e-6f);
    CHECK(sunLevelAt(0.2f * DAY_LENGTH) == 1.0f);
    float prev = sunLevelAt(0.0f);
    for (int i = 1; i <= 200; ++i) { // no jumps anywhere in the cycle
        float t = DAY_LENGTH * i / 200.0f;
        float v = sunLevelAt(t);
        CHECK(v >= NIGHT_SUN - 1e-6f && v <= 1.0f + 1e-6f);
        CHECK(std::fabs(v - prev) < 0.12f);
        prev = v;
    }
    // Wraps across day boundaries (the clock only ever grows).
    CHECK(std::fabs(sunLevelAt(2.3f * DAY_LENGTH) - sunLevelAt(0.3f * DAY_LENGTH)) < 1e-6f);
    // Sky follows: bright at noon, dark at midnight, reddish at dusk.
    glm::vec3 noon = skyColorAt(0.2f * DAY_LENGTH);
    glm::vec3 mid = skyColorAt(0.7f * DAY_LENGTH);
    glm::vec3 dusk = skyColorAt(0.45f * DAY_LENGTH);
    CHECK(noon.b > 0.8f && mid.b < 0.1f);
    CHECK(dusk.r > dusk.b); // horizon tint
}

static void testLightChannelSplit() {
    // A torch-lit cell next to a sunlit one: the mesh must carry the two
    // channels separately so night can dim sun without dimming the torch.
    ChunkSnapshot s;
    s.blocks.assign(Chunk::rawSize(), Block::Air);
    s.light.assign(Chunk::rawSize(), 0);
    auto at = [](int x, int y, int z) { return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x; };
    s.blocks[at(8, 40, 8)] = Block::Stone;
    s.light[at(8, 41, 8)] = uint8_t(14 << 4); // strong block light, no sun
    MeshData md = buildMeshData(s);
    int top = topQuadOf(md.verts, 8, 40, 8);
    CHECK(top >= 0);
    const ChunkVertex& v = md.verts[size_t(top) * 4];
    CHECK(v.blk > v.sun);  // torch-lit: block channel dominates
    CHECK(v.sun > 0);      // dark-sun floor, not zero

    s.light[at(8, 41, 8)] = 15; // full sun instead
    md = buildMeshData(s);
    top = topQuadOf(md.verts, 8, 40, 8);
    const ChunkVertex& w = md.verts[size_t(top) * 4];
    CHECK(w.sun > w.blk);
}

static void testLevelDayTime() {
    const char* dir = "test_saves_day";
    std::filesystem::remove_all(dir);
    {
        World w(7, dir);
        CHECK(w.dayTime() == 0.0f); // fresh world starts in the morning
        w.setDayTime(123.5f);
        w.saveAllModified();
    }
    {
        World w(7, dir);
        CHECK(std::fabs(w.dayTime() - 123.5f) < 1e-6f); // clock persisted
    }
    // A v1 level.bin (no day clock) migrates: seed kept, clock at morning.
    {
        std::ofstream f(std::string(dir) + "/level.bin", std::ios::binary);
        f.write("MCLV", 4);
        uint32_t ver = 1, seed = 4242;
        f.write(reinterpret_cast<const char*>(&ver), 4);
        f.write(reinterpret_cast<const char*>(&seed), 4);
    }
    {
        World w(7, dir);
        CHECK(w.seed() == 4242);
        CHECK(w.dayTime() == 0.0f);
    }
    std::filesystem::remove_all(dir);
}

static void testKeyBinds() {
    CHECK(keys::fromName("W") == 'W');
    CHECK(keys::fromName("w") == 'W'); // case-insensitive
    CHECK(keys::fromName("SPACE") == keys::SPACE);
    CHECK(keys::fromName("lshift") == keys::LSHIFT);
    CHECK(keys::fromName("nosuchkey") == -1);
    CHECK(keys::toName('W') == "W");
    CHECK(keys::toName(keys::LCTRL) == "LCTRL");

    // Custom binds parse; bad names/actions warn and keep the default.
    {
        std::ofstream f("test_settings.cfg");
        f << "key_forward=Z\nkey_jump=TAB\nkey_back=NOSUCH\nkey_dance=Q\n";
    }
    Settings s = Settings::load("test_settings.cfg");
    CHECK(s.keyForward == 'Z');
    CHECK(s.keyJump == keys::TAB);
    CHECK(s.keyBack == 'S');

    // Save/load round-trips every bind.
    s.keySneak = keys::CAPSLOCK;
    s.save("test_settings.cfg");
    Settings t = Settings::load("test_settings.cfg");
    CHECK(t.keyForward == 'Z' && t.keyJump == keys::TAB);
    CHECK(t.keySneak == keys::CAPSLOCK && t.keyInventory == 'E');
    std::filesystem::remove("test_settings.cfg");
}

static void testSoundBank() {
    // Every breakable block must map to a real material bank — a block
    // without one breaks silently (user feedback: dirt must not clink).
    for (int i = 0; i < BLOCK_TYPES; ++i) {
        Block b = Block(i);
        if (isBreakable(b)) CHECK(soundMaterial(b) != SoundMat::None);
    }
    CHECK(soundMaterial(Block::Dirt) == SoundMat::Soft);
    CHECK(soundMaterial(Block::Stone) == SoundMat::Stone);
    CHECK(soundMaterial(Block::Water) == SoundMat::None);

    // The embedded CC0 samples decode to sane buffers: every bank has
    // multiple variants, each non-trivial, peak-normalized, with the
    // trailing silence trimmed by the generator script.
    for (int bank = 0; bank < SOUND_BANK_COUNT; ++bank) {
        auto variants = soundVariants(bank);
        CHECK(variants.size() >= 2);
        for (const auto& s : variants) {
            CHECK(s.size() > size_t(SOUND_RATE) / 50); // at least 20 ms
            CHECK(s.size() < size_t(SOUND_RATE) * 2);  // and under 2 s
            float peak = 0.0f;
            for (float v : s) peak = std::max(peak, std::fabs(v));
            CHECK(std::fabs(peak - 0.8f) < 0.01f); // normalized loudness
            float tail = 0.0f; // last 5 ms — generator trims dead air
            for (size_t i = s.size() - SOUND_RATE / 200; i < s.size(); ++i)
                tail = std::max(tail, std::fabs(s[i]));
            CHECK(tail < 0.25f);
        }
    }
    // Decoding is exact little-endian s16: a known two-sample buffer.
    const unsigned char raw[] = {0x00, 0x40, 0x00, 0xC0}; // +0.5, -0.5
    std::vector<float> d = decodeSound(raw, 4, 0.5f);
    CHECK(d.size() == 2);
    CHECK(std::fabs(d[0] - 0.5f) < 1e-3f && std::fabs(d[1] + 0.5f) < 1e-3f);
}

static void testMenuUiHitTesting() {
    const int w = 800, h = 600;

    ui::Rect resume = ui::menuButtonRect(w, h, 0);
    ui::MenuCommand c = ui::hitTestMenu(ui::MenuPage::Main, w, h,
                                        resume.x + resume.w * 0.5f,
                                        resume.y + resume.h * 0.5f);
    CHECK(c.action == ui::MenuAction::Resume);

    ui::Rect settings = ui::menuButtonRect(w, h, 1);
    c = ui::hitTestMenu(ui::MenuPage::Main, w, h,
                        settings.x + settings.w * 0.5f,
                        settings.y + settings.h * 0.5f);
    CHECK(c.action == ui::MenuAction::OpenSettings);

    ui::Rect quit = ui::menuButtonRect(w, h, 2);
    c = ui::hitTestMenu(ui::MenuPage::Main, w, h,
                        quit.x + quit.w * 0.5f,
                        quit.y + quit.h * 0.5f);
    CHECK(c.action == ui::MenuAction::Quit);

    ui::Rect row = ui::settingsRowRect(w, h, int(ui::SettingId::RenderDistance));
    ui::Rect dec = ui::settingsDecRect(row);
    c = ui::hitTestMenu(ui::MenuPage::Settings, w, h,
                        dec.x + dec.w * 0.5f, dec.y + dec.h * 0.5f);
    CHECK(c.action == ui::MenuAction::AdjustSetting);
    CHECK(c.setting == ui::SettingId::RenderDistance);
    CHECK(c.dir == -1);

    ui::Rect inc = ui::settingsIncRect(row);
    c = ui::hitTestMenu(ui::MenuPage::Settings, w, h,
                        inc.x + inc.w * 0.5f, inc.y + inc.h * 0.5f);
    CHECK(c.action == ui::MenuAction::AdjustSetting);
    CHECK(c.setting == ui::SettingId::RenderDistance);
    CHECK(c.dir == 1);

    ui::Rect back = ui::settingsBackRect(w, h);
    c = ui::hitTestMenu(ui::MenuPage::Settings, w, h,
                        back.x + back.w * 0.5f, back.y + back.h * 0.5f);
    CHECK(c.action == ui::MenuAction::Back);

    c = ui::hitTestMenu(ui::MenuPage::None, w, h, resume.x, resume.y);
    CHECK(c.action == ui::MenuAction::None);
}

static void testMenuUiAdjustSettings() {
    Settings s;
    std::vector<int> fps = {30, 60, 144, 0};

    s.renderDistance = 16;
    ui::SettingEffects e = ui::adjustSetting(s, ui::SettingId::RenderDistance, 1, fps);
    CHECK(s.renderDistance == 16);
    CHECK(e.saveSettings);

    s.renderDistance = 2;
    e = ui::adjustSetting(s, ui::SettingId::RenderDistance, -1, fps);
    CHECK(s.renderDistance == 2);

    s.fov = 75.0f;
    e = ui::adjustSetting(s, ui::SettingId::Fov, 1, fps);
    CHECK(std::fabs(s.fov - 80.0f) < 0.001f);
    CHECK(!e.mouseSensitivityChanged && !e.volumeChanged && !e.vsyncChanged);

    s.mouseSensitivity = 0.12f;
    e = ui::adjustSetting(s, ui::SettingId::MouseSensitivity, 1, fps);
    CHECK(std::fabs(s.mouseSensitivity - 0.14f) < 0.001f);
    CHECK(e.mouseSensitivityChanged && e.saveSettings);

    s.volume = 0.8f;
    e = ui::adjustSetting(s, ui::SettingId::Volume, -1, fps);
    CHECK(std::fabs(s.volume - 0.7f) < 0.001f);
    CHECK(e.volumeChanged && e.saveSettings);

    s.vsync = true;
    e = ui::adjustSetting(s, ui::SettingId::Vsync, 1, fps);
    CHECK(!s.vsync);
    CHECK(e.vsyncChanged && e.saveSettings);

    s.fpsMax = 60;
    e = ui::adjustSetting(s, ui::SettingId::FpsMax, 1, fps);
    CHECK(s.fpsMax == 144);
    e = ui::adjustSetting(s, ui::SettingId::FpsMax, -1, fps);
    CHECK(s.fpsMax == 60);

    s.volume = 0.7f;
    CHECK(ui::settingValueText(s, ui::SettingId::Volume) == "70%");
    s.fpsMax = 0;
    CHECK(ui::settingValueText(s, ui::SettingId::FpsMax) == "unlimited");
}

static void testMenuUiInventoryHelpers() {
    const int w = 800, h = 600;
    ui::InventoryLayout L = ui::inventoryLayout(w, h);
    ui::Rect slot0 = ui::inventorySlotRect(L, 0);
    ui::Rect slot8 = ui::inventorySlotRect(L, 8);
    CHECK(slot0.y > slot8.y); // hotbar row is drawn below the grid
    CHECK(ui::inventorySlotAt(w, h, slot0.x + 4.0f, slot0.y + 4.0f) == 0);
    CHECK(ui::inventorySlotAt(w, h, L.x0 - 1.0f, L.y0 - 1.0f) == -1);

    Inventory inv;
    ItemStack cursor{ItemId::DirtBlock, 10, 0};
    ui::clickInventorySlot(inv, cursor, 0);
    CHECK(inv.slots[0].item == ItemId::DirtBlock && inv.slots[0].count == 10);
    CHECK(cursor.empty());

    cursor = {ItemId::DirtBlock, 60, 0};
    ui::clickInventorySlot(inv, cursor, 0);
    CHECK(inv.slots[0].count == Inventory::STACK_MAX);
    CHECK(cursor.item == ItemId::DirtBlock && cursor.count == 6);

    inv.slots[1] = {ItemId::StoneBlock, 3, 0};
    ui::clickInventorySlot(inv, cursor, 1);
    CHECK(inv.slots[1].item == ItemId::DirtBlock && inv.slots[1].count == 6);
    CHECK(cursor.item == ItemId::StoneBlock && cursor.count == 3);
}

static void testMenuUiStackClickHelpers() {
    ItemStack slot = makeItemStack(ItemId::Coal, 9);
    ItemStack cursor;
    ui::clickStack(slot, cursor, ui::ClickButton::Right);
    CHECK(cursor.item == ItemId::Coal && cursor.count == 5);
    CHECK(slot.item == ItemId::Coal && slot.count == 4);

    ui::clickStack(slot, cursor, ui::ClickButton::Right);
    CHECK(slot.count == 5);
    CHECK(cursor.count == 4);

    ItemStack other = makeItemStack(ItemId::IronIngot, 1);
    ui::clickStack(other, cursor, ui::ClickButton::Right);
    CHECK(other.item == ItemId::IronIngot && other.count == 1);
    CHECK(cursor.item == ItemId::Coal && cursor.count == 4);

    ui::clickStack(slot, cursor, ui::ClickButton::Left);
    CHECK(slot.item == ItemId::Coal && slot.count == 9);
    CHECK(cursor.empty());
}

static void testMenuUiCraftingSlotsAndOutput() {
    ui::CraftingUiState state(2);
    CHECK(ui::craftingSlotAt(state, 0, 0) == ui::UiSlot::craft(0));
    CHECK(ui::craftingSlotAt(state, 1, 1) == ui::UiSlot::craft(3));
    CHECK(ui::craftingOutputSlot() == ui::UiSlot::craftOutput());

    state.grid.at(0, 0) = makeItemStack(ItemId::LogBlock, 2);
    ItemStack cursor;
    CHECK(ui::clickCraftingOutput(state, cursor, ui::ClickButton::Left));
    CHECK(cursor.item == ItemId::PlanksBlock && cursor.count == 4);
    CHECK(state.grid.at(0, 0).count == 1);

    cursor = makeItemStack(ItemId::Coal, 1);
    CHECK(!ui::clickCraftingOutput(state, cursor, ui::ClickButton::Right));
    CHECK(cursor.item == ItemId::Coal && cursor.count == 1);
    CHECK(state.grid.at(0, 0).count == 1);
}

static void testMenuUiShiftClickDestinations() {
    Inventory inv;
    crafting::CraftingGrid craftGrid = grid(2);
    craftGrid.at(0, 0) = makeItemStack(ItemId::Coal, 3);
    CHECK(ui::quickMoveFromCraftingGrid(craftGrid, 0, inv));
    CHECK(craftGrid.at(0, 0).empty());
    CHECK(inv.slots[0].item == ItemId::Coal && inv.slots[0].count == 3);

    ui::CraftingUiState craftOut(2);
    craftOut.grid.at(0, 0) = makeItemStack(ItemId::LogBlock, 1);
    CHECK(ui::quickMoveCraftingOutput(craftOut, inv));
    CHECK(craftOut.grid.at(0, 0).empty());
    CHECK(inv.slots[1].item == ItemId::PlanksBlock && inv.slots[1].count == 4);

    ui::CraftingUiState blockedOut(2);
    blockedOut.grid.at(0, 0) = makeItemStack(ItemId::LogBlock, 1);
    Inventory fullInv;
    for (int i = 0; i < Inventory::SLOTS; ++i)
        fullInv.slots[i] = makeItemStack(ItemId::StoneBlock, 64);
    CHECK(!ui::quickMoveCraftingOutput(blockedOut, fullInv));
    CHECK(blockedOut.grid.at(0, 0).item == ItemId::LogBlock);

    FurnaceState furnace;
    furnace.input = makeItemStack(ItemId::RawIron, 2);
    furnace.fuel = makeItemStack(ItemId::Coal, 2);
    furnace.output = makeItemStack(ItemId::IronIngot, 2);
    CHECK(ui::quickMoveFromFurnace(furnace, ui::FurnaceSlot::Output, inv));
    CHECK(furnace.output.empty());
    bool sawIngot = false;
    for (const ItemStack& s : inv.slots)
        if (s.item == ItemId::IronIngot && s.count == 2) sawIngot = true;
    CHECK(sawIngot);

    Inventory fuelInv;
    fuelInv.slots[0] = makeItemStack(ItemId::Coal, 1);
    CHECK(ui::quickMoveInventoryToFurnace(fuelInv, 0, furnace));
    CHECK(furnace.fuel.item == ItemId::Coal && furnace.fuel.count == 3);
    CHECK(fuelInv.slots[0].empty());
}

static void testMenuUiRecipeReferenceEnumeratesCraftingTable() {
    std::vector<ItemStack> outputs = ui::recipeReferenceOutputs();
    bool sawPick = false, sawFurnace = false, sawTorch = false;
    for (ItemStack out : outputs) {
        if (out.item == ItemId::IronPickaxe) sawPick = true;
        if (out.item == ItemId::FurnaceBlock) sawFurnace = true;
        if (out.item == ItemId::TorchBlock) sawTorch = true;
    }
    CHECK(sawPick && sawFurnace && sawTorch);
    CHECK(outputs.size() == crafting::recipeCount());
}

static void testTickClockRunsFixedTicksAndAlpha() {
    TickClock clock;

    CHECK(clock.advance(0.016, false) == 0);
    CHECK(std::fabs(clock.consumedSeconds()) < 1e-9);
    CHECK(std::fabs(clock.alpha() - 0.32f) < 0.001f);

    CHECK(clock.advance(0.034, false) == 1);
    CHECK(std::fabs(clock.consumedSeconds() - TickClock::TICK_DT) < 1e-9);
    CHECK(std::fabs(clock.alpha()) < 0.001f);

    CHECK(clock.advance(0.126, false) == 2);
    CHECK(std::fabs(clock.consumedSeconds() - 2.0 * TickClock::TICK_DT) < 1e-9);
    CHECK(std::fabs(clock.alpha() - 0.52f) < 0.001f);
}

static void testTickClockCapsStallsAndDropsRemainder() {
    TickClock clock;

    CHECK(clock.advance(2.0, false) == 5);
    CHECK(std::fabs(clock.consumedSeconds() - 5.0 * TickClock::TICK_DT) < 1e-9);
    CHECK(std::fabs(clock.alpha()) < 0.001f);

    CHECK(clock.advance(0.05, false) == 1);
    CHECK(std::fabs(clock.consumedSeconds() - TickClock::TICK_DT) < 1e-9);
    CHECK(std::fabs(clock.alpha()) < 0.001f);
}

static void testTickClockPauseFreezesAccumulatedTime() {
    TickClock clock;

    CHECK(clock.advance(0.025, false) == 0);
    float before = clock.alpha();
    CHECK(clock.advance(1.0, true) == 0);
    CHECK(std::fabs(clock.consumedSeconds()) < 1e-9);
    CHECK(std::fabs(clock.alpha() - before) < 0.001f);

    CHECK(clock.advance(0.024, false) == 0);
    CHECK(clock.advance(0.001, false) == 1);
    CHECK(std::fabs(clock.consumedSeconds() - TickClock::TICK_DT) < 1e-9);
    CHECK(std::fabs(clock.alpha()) < 0.001f);
}

int main() {
    testFloorDivMod();
    testMeshData();
    testGreedyMerging();
    testTorchMesh();
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
    testLightingAccessorTorchRelight();
    testTerrainDeterminism();
    testTerrainShape();
    testTreeBorderConsistency();
    testCaves();
    testOres();
    testSaveLoadRoundTrip();
    testLevelSeed();
    testWorldSaveChunkFormat();
    testWorldSaveLevelFormat();
    testAtomicSaveOverwritesExisting();
    testUnloadSaves();
    testRaycast();
    testBodyPhysics();
    testMoveBodyAxisFlushBoundaries();
    testPlayerOwnsCanonicalBody();
    testPlayerLandsOnPlatform();
    testItemRegistry();
    testInventory();
    testMiningRequiredTicksAndDrops();
    testMiningDurabilityUse();
    testMiningProgressResetsAndBreakEvent();
    testCraftingRecipeMatching();
    testCraftingConsumptionAndCursorOutput();
    testFurnaceSmeltingTicksAndFuel();
    testFurnaceBlockedAndMissingInputBurns();
    testFurnaceSaveLoadAndRemoval();
    testWorldOwnsFurnaceState();
    testFurnaceBreakTakesContents();
    testWrongToolFurnaceBreakStillTakesContents();
    testItemEntityFallsAndLands();
    testItemPickup();
    testItemPickupPreservesDurabilityAndRemainder();
    testItemFrozenInUnloadedChunk();
    testItemEntityMerging();
    testEntityBucketsAndDrops();
    testNonBlockItemIconMapping();
    testBreakOverlayHelpers();
    testItemEntityStackIngress();
    testPlayerSaveV3Roundtrip();
    testPlayerSaveV2BlockInventoryMigrates();
    testPlayerSaveV3UnknownItemSanitizes();
    testPlayerSaveV1Migrates();
    testKeyBinds();
    testSoundBank();
    testMenuUiHitTesting();
    testMenuUiAdjustSettings();
    testMenuUiInventoryHelpers();
    testMenuUiStackClickHelpers();
    testMenuUiCraftingSlotsAndOutput();
    testMenuUiShiftClickDestinations();
    testMenuUiRecipeReferenceEnumeratesCraftingTable();
    testTickClockRunsFixedTicksAndAlpha();
    testTickClockCapsStallsAndDropsRemainder();
    testTickClockPauseFreezesAccumulatedTime();
    testDayCycle();
    testLightChannelSplit();
    testLevelDayTime();
    if (failures == 0) std::printf("all tests passed\n");
    return failures == 0 ? 0 : 1;
}
