#include "World.h"
#include "SaveIO.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {
int workerCount() {
    int hw = (int)std::thread::hardware_concurrency();
    return std::clamp(hw - 1, 1, 4);
}
using Clock = std::chrono::steady_clock;
float msSince(Clock::time_point t0) {
    return std::chrono::duration<float, std::milli>(Clock::now() - t0).count();
}
// Exponential moving average for perf counters.
void ema(std::atomic<float>& avg, float sample) {
    float cur = avg.load(std::memory_order_relaxed);
    avg.store(cur == 0.0f ? sample : cur * 0.9f + sample * 0.1f, std::memory_order_relaxed);
}
}

World::World(uint32_t seed, std::string saveDir)
    : seed_(loadOrCreateSeed(saveDir, seed)), terrain_(seed_),
      saveDir_(std::move(saveDir)), pool_(workerCount()) {
    loadDayTime();
}

namespace {
constexpr char LEVEL_MAGIC[4] = {'M', 'C', 'L', 'V'};
// v1: magic + version + seed. v2 appends the day clock (float seconds);
// v1 files load fine (seed kept, clock starts at morning).
constexpr uint32_t LEVEL_VERSION = 2;
}

// The seed lives in level.bin so a save directory stays valid even if the
// caller's default seed changes (unmodified chunks regenerate from the seed).
// Missing or corrupt file: adopt the fallback and (re)write it. Old versions
// migrate — rewriting them with the fallback would silently swap the
// terrain under an existing save.
uint32_t World::loadOrCreateSeed(const std::string& saveDir, uint32_t fallback) {
    std::filesystem::create_directories(saveDir);
    std::string path = saveDir + "/level.bin";
    {
        std::ifstream f(path, std::ios::binary);
        if (f) {
            char magic[4];
            uint32_t version = 0, seed = 0;
            f.read(magic, 4);
            f.read(reinterpret_cast<char*>(&version), 4);
            f.read(reinterpret_cast<char*>(&seed), 4);
            if (f && std::memcmp(magic, LEVEL_MAGIC, 4) == 0 &&
                version >= 1 && version <= LEVEL_VERSION)
                return seed;
            std::fprintf(stderr, "warning: bad level.bin, rewriting with seed %u\n",
                         fallback);
        }
    }
    if (!atomicSave(path, [&](std::ofstream& f) {
            float dayTime = 0.0f;
            f.write(LEVEL_MAGIC, 4);
            f.write(reinterpret_cast<const char*>(&LEVEL_VERSION), 4);
            f.write(reinterpret_cast<const char*>(&fallback), 4);
            f.write(reinterpret_cast<const char*>(&dayTime), 4);
        }))
        std::fprintf(stderr, "warning: failed to write level.bin\n");
    return fallback;
}

void World::loadDayTime() {
    std::ifstream f(saveDir_ + "/level.bin", std::ios::binary);
    if (!f) return;
    char magic[4];
    uint32_t version = 0, seed = 0;
    float t = 0.0f;
    f.read(magic, 4);
    f.read(reinterpret_cast<char*>(&version), 4);
    f.read(reinterpret_cast<char*>(&seed), 4);
    if (!f || std::memcmp(magic, LEVEL_MAGIC, 4) != 0 || version < 2) return;
    f.read(reinterpret_cast<char*>(&t), 4);
    if (f) dayTime_ = t;
}

void World::saveLevel() const {
    if (!atomicSave(saveDir_ + "/level.bin", [&](std::ofstream& f) {
            f.write(LEVEL_MAGIC, 4);
            f.write(reinterpret_cast<const char*>(&LEVEL_VERSION), 4);
            f.write(reinterpret_cast<const char*>(&seed_), 4);
            f.write(reinterpret_cast<const char*>(&dayTime_), 4);
        }))
        std::fprintf(stderr, "warning: failed to write level.bin\n");
}

World::~World() { saveAllModified(); }

Chunk* World::getChunk(int cx, int cz) const {
    auto it = chunks_.find({cx, cz});
    return it == chunks_.end() ? nullptr : it->second.get();
}

Block World::getBlock(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CHUNK_HEIGHT) return Block::Air;
    int cx = floorDiv(wx, CHUNK_SIZE), cz = floorDiv(wz, CHUNK_SIZE);
    Chunk* c = getChunk(cx, cz);
    if (!c) return Block::Air;
    return c->get(mod(wx, CHUNK_SIZE), wy, mod(wz, CHUNK_SIZE));
}

void World::setBlock(int wx, int wy, int wz, Block b) {
    if (wy < 0 || wy >= CHUNK_HEIGHT) return;
    int cx = floorDiv(wx, CHUNK_SIZE), cz = floorDiv(wz, CHUNK_SIZE);
    Chunk* c = getChunk(cx, cz);
    if (!c) return;
    int lx = mod(wx, CHUNK_SIZE), lz = mod(wz, CHUNK_SIZE);
    Block old = c->get(lx, wy, lz);
    if (old == b) return;
    c->set(lx, wy, lz, b);
    c->dirty = true;
    c->modified = true;
    // Border block changes affect the neighbor's mesh too.
    markBorderDirty(cx, cz, lx, lz);

    // Incremental relight (chunks touched by the BFS mark themselves dirty).
    glm::ivec3 p(wx, wy, wz);
    std::vector<glm::ivec3> around = {
        {wx + 1, wy, wz}, {wx - 1, wy, wz}, {wx, wy + 1, wz},
        {wx, wy - 1, wz}, {wx, wy, wz + 1}, {wx, wy, wz - 1},
    };
    // Block-light channel: an old emitter or a newly opaque cell loses its
    // light; a new emitter seeds; a newly transparent cell pulls from around.
    if (lightEmission(old) > 0 || (isOpaque(b) && !isOpaque(old)))
        removeLight(LightChan::Block, p);
    if (lightEmission(b) > 0) {
        setLight(LightChan::Block, wx, wy, wz, lightEmission(b));
        addLight(LightChan::Block, {p});
    } else if (!isOpaque(b)) {
        addLight(LightChan::Block, around);
    }
    // Sun channel: same shape, but sources are only the sky. Water counts as
    // a (partial) blocker: placing it must strip the lossless 15 column below,
    // after which the re-add seeds give it attenuated light.
    bool blocksSunNow = isOpaque(b) || dimsSunlight(b);
    bool blockedSunBefore = isOpaque(old) || dimsSunlight(old);
    if (blocksSunNow && !blockedSunBefore)
        removeLight(LightChan::Sun, p);
    if (!isOpaque(b))
        addLight(LightChan::Sun, around);
}

uint8_t World::sunLightAt(int wx, int wy, int wz) const {
    return getLight(LightChan::Sun, wx, wy, wz);
}
uint8_t World::blockLightAt(int wx, int wy, int wz) const {
    return getLight(LightChan::Block, wx, wy, wz);
}

uint8_t World::getLight(LightChan ch, int wx, int wy, int wz) const {
    if (wy >= CHUNK_HEIGHT) return ch == LightChan::Sun ? 15 : 0;
    if (wy < 0) return 0;
    Chunk* c = getChunk(floorDiv(wx, CHUNK_SIZE), floorDiv(wz, CHUNK_SIZE));
    if (!c) return 0;
    int lx = mod(wx, CHUNK_SIZE), lz = mod(wz, CHUNK_SIZE);
    return ch == LightChan::Sun ? c->sunLight(lx, wy, lz) : c->blockLight(lx, wy, lz);
}

void World::setLight(LightChan ch, int wx, int wy, int wz, uint8_t v) {
    if (wy < 0 || wy >= CHUNK_HEIGHT) return;
    int cx = floorDiv(wx, CHUNK_SIZE), cz = floorDiv(wz, CHUNK_SIZE);
    Chunk* c = getChunk(cx, cz);
    if (!c) return;
    int lx = mod(wx, CHUNK_SIZE), lz = mod(wz, CHUNK_SIZE);
    uint8_t cur = ch == LightChan::Sun ? c->sunLight(lx, wy, lz) : c->blockLight(lx, wy, lz);
    if (cur == v) return;
    if (ch == LightChan::Sun) c->setSunLight(lx, wy, lz, v);
    else                      c->setBlockLight(lx, wy, lz, v);
    c->dirty = true;
    // Border cells light the neighbor chunk's faces, so its mesh is stale too.
    markBorderDirty(cx, cz, lx, lz);
}

// A changed border cell invalidates the meshes that can see it: the face
// neighbor(s), and — because AO/smooth lighting sample diagonally — the
// diagonal neighbor when the cell sits on a chunk corner.
void World::markBorderDirty(int cx, int cz, int lx, int lz) {
    bool xn = lx == 0, xp = lx == CHUNK_SIZE - 1;
    bool zn = lz == 0, zp = lz == CHUNK_SIZE - 1;
    auto mark = [&](int dx, int dz) {
        if (Chunk* n = getChunk(cx + dx, cz + dz)) n->dirty = true;
    };
    if (xn) mark(-1, 0);
    if (xp) mark(1, 0);
    if (zn) mark(0, -1);
    if (zp) mark(0, 1);
    if (xn && zn) mark(-1, -1);
    if (xn && zp) mark(-1, 1);
    if (xp && zn) mark(1, -1);
    if (xp && zp) mark(1, 1);
}

namespace {
const glm::ivec3 LIGHT_DIRS[6] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};
}

// Flood-fill from cells whose light is already set; spreads only into loaded,
// transparent cells, losing one level per step (except full sun going down).
void World::addLight(LightChan ch, std::vector<glm::ivec3> q) {
    for (size_t h = 0; h < q.size(); ++h) {
        glm::ivec3 p = q[h];
        uint8_t l = getLight(ch, p.x, p.y, p.z);
        if (l <= 1) continue;
        for (const glm::ivec3& d : LIGHT_DIRS) {
            glm::ivec3 n = p + d;
            if (n.y < 0 || n.y >= CHUNK_HEIGHT) continue;
            if (!getChunk(floorDiv(n.x, CHUNK_SIZE), floorDiv(n.z, CHUNK_SIZE))) continue;
            Block nb = getBlock(n.x, n.y, n.z);
            if (isOpaque(nb)) continue;
            uint8_t target = (ch == LightChan::Sun && d.y == -1 && l == 15 &&
                              !dimsSunlight(nb)) ? 15 : l - 1;
            if (getLight(ch, n.x, n.y, n.z) < target) {
                setLight(ch, n.x, n.y, n.z, target);
                q.push_back(n);
            }
        }
    }
}

// Standard unlight BFS: zero the cell, strip every neighbor whose light could
// only have come from it, remember brighter cells as re-add seeds.
void World::removeLight(LightChan ch, const glm::ivec3& pos) {
    uint8_t old = getLight(ch, pos.x, pos.y, pos.z);
    if (old == 0) return;
    setLight(ch, pos.x, pos.y, pos.z, 0);
    std::vector<std::pair<glm::ivec3, uint8_t>> q{{pos, old}};
    std::vector<glm::ivec3> readd;
    for (size_t h = 0; h < q.size(); ++h) {
        glm::ivec3 p = q[h].first;
        uint8_t l = q[h].second;
        for (const glm::ivec3& d : LIGHT_DIRS) {
            glm::ivec3 n = p + d;
            if (n.y < 0 || n.y >= CHUNK_HEIGHT) continue;
            uint8_t nl = getLight(ch, n.x, n.y, n.z);
            if (nl == 0) continue;
            // Sun level 15 propagates down lossless, so a 15 below a removed
            // 15 also depends on it despite not being dimmer.
            if (nl < l || (ch == LightChan::Sun && d.y == -1 && l == 15)) {
                setLight(ch, n.x, n.y, n.z, 0);
                q.push_back({n, nl});
            } else {
                readd.push_back(n);
            }
        }
    }
    addLight(ch, std::move(readd));
}

// A newly integrated chunk and its neighbors exchange light across the shared
// faces (each chunk's initial light assumed dark neighbors; this is add-only).
void World::seedChunkBorderLight(int cx, int cz) {
    std::vector<glm::ivec3> sunSeeds, blkSeeds;
    int bx = cx * CHUNK_SIZE, bz = cz * CHUNK_SIZE;
    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto& d : dirs) {
        if (!getChunk(cx + d[0], cz + d[1])) continue;
        for (int i = 0; i < CHUNK_SIZE; ++i) {
            int ix = d[0] == 1 ? bx + CHUNK_SIZE - 1 : d[0] == -1 ? bx : bx + i;
            int iz = d[1] == 1 ? bz + CHUNK_SIZE - 1 : d[1] == -1 ? bz : bz + i;
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                glm::ivec3 in(ix, y, iz);
                glm::ivec3 out(ix + d[0], y, iz + d[1]);
                for (const glm::ivec3& p : {in, out}) {
                    if (getLight(LightChan::Sun, p.x, p.y, p.z) > 1) sunSeeds.push_back(p);
                    if (getLight(LightChan::Block, p.x, p.y, p.z) > 1) blkSeeds.push_back(p);
                }
            }
        }
    }
    addLight(LightChan::Sun, std::move(sunSeeds));
    addLight(LightChan::Block, std::move(blkSeeds));
}

void World::markNeighborsDirty(int cx, int cz) {
    // Diagonals included: a new chunk's corner columns feed the AO/smooth
    // lighting of all eight surrounding meshes.
    static const int d[8][2] = {{1,0},{-1,0},{0,1},{0,-1},
                                {1,1},{1,-1},{-1,1},{-1,-1}};
    for (auto& o : d)
        if (Chunk* n = getChunk(cx + o[0], cz + o[1])) n->dirty = true;
}

std::string World::chunkPath(int cx, int cz) const {
    return saveDir_ + "/c_" + std::to_string(cx) + "_" + std::to_string(cz) + ".bin";
}

namespace {
constexpr char CHUNK_MAGIC[4] = {'M', 'C', 'C', 'H'};
constexpr uint32_t CHUNK_VERSION = 1;
}

bool World::loadChunkFromDisk(Chunk& c) const {
    std::ifstream f(chunkPath(c.cx(), c.cz()), std::ios::binary);
    if (!f) return false;
    char magic[4];
    uint32_t version = 0;
    f.read(magic, 4);
    f.read(reinterpret_cast<char*>(&version), 4);
    if (!f || std::memcmp(magic, CHUNK_MAGIC, 4) != 0 || version != CHUNK_VERSION) {
        std::fprintf(stderr, "warning: chunk %d,%d has bad/old save format, regenerating\n",
                     c.cx(), c.cz());
        return false;
    }
    f.read(reinterpret_cast<char*>(c.rawData()), Chunk::rawSize());
    if (f.gcount() != (std::streamsize)Chunk::rawSize()) return false;
    // Saved bytes index straight into BLOCK_DEFS: clamp unknown ids (file
    // from a newer build, or corruption) to Air instead of reading off the
    // end of the registry.
    uint8_t* raw = c.rawData();
    for (size_t i = 0; i < Chunk::rawSize(); ++i)
        if (raw[i] >= BLOCK_TYPES) raw[i] = uint8_t(Block::Air);
    return true;
}

void World::saveChunk(const Chunk& c) {
    bool ok = atomicSave(chunkPath(c.cx(), c.cz()), [&](std::ofstream& f) {
        f.write(CHUNK_MAGIC, 4);
        f.write(reinterpret_cast<const char*>(&CHUNK_VERSION), 4);
        f.write(reinterpret_cast<const char*>(c.rawData()), Chunk::rawSize());
    });
    if (!ok)
        std::fprintf(stderr, "warning: failed to save chunk %d,%d\n", c.cx(), c.cz());
}

void World::update(const glm::vec3& playerPos, int renderDistance) {
    // 1. Integrate chunks finished by workers.
    std::vector<std::pair<ChunkKey, std::unique_ptr<Chunk>>> done;
    {
        std::lock_guard<std::mutex> l(genM_);
        done.swap(genDone_);
    }
    for (auto& [key, chunk] : done) {
        pendingGen_.erase(key);
        chunk->dirty = true;
        markNeighborsDirty(key.x, key.z); // their border faces may now be hidden
        chunks_[key] = std::move(chunk);
        seedChunkBorderLight(key.x, key.z); // exchange light with neighbors
    }

    int pcx = floorDiv((int)std::floor(playerPos.x), CHUNK_SIZE);
    int pcz = floorDiv((int)std::floor(playerPos.z), CHUNK_SIZE);

    // 2. Request missing chunks nearest-first, keeping a bounded backlog.
    int budget = 8 - (int)pendingGen_.size();
    for (int r = 0; r <= renderDistance && budget > 0; ++r) {
        for (int dz = -r; dz <= r && budget > 0; ++dz) {
            for (int dx = -r; dx <= r && budget > 0; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                ChunkKey key{pcx + dx, pcz + dz};
                if (chunks_.count(key) || pendingGen_.count(key)) continue;
                pendingGen_.insert(key);
                --budget;
                pool_.push([this, key] {
                    auto t0 = Clock::now();
                    auto c = std::make_unique<Chunk>(key.x, key.z);
                    if (!loadChunkFromDisk(*c)) terrain_.generateChunk(*c);
                    c->computeInitialLight(); // light is never saved; rebuild it
                    ema(genMs_, msSince(t0));
                    std::lock_guard<std::mutex> l(genM_);
                    genDone_.emplace_back(key, std::move(c));
                });
            }
        }
    }

    // 3. Unload far chunks (save modified ones first).
    for (auto it = chunks_.begin(); it != chunks_.end();) {
        int dx = std::abs(it->first.x - pcx), dz = std::abs(it->first.z - pcz);
        if (std::max(dx, dz) > renderDistance + 2) {
            if (it->second->modified) saveChunk(*it->second);
            it = chunks_.erase(it);
        } else {
            ++it;
        }
    }
}

ChunkSnapshot World::snapshot(const Chunk& c) const {
    ChunkSnapshot s;
    s.cx = c.cx();
    s.cz = c.cz();
    const Block* data = reinterpret_cast<const Block*>(c.rawData());
    s.blocks.assign(data, data + Chunk::rawSize());
    s.light.resize(s.blocks.size());
    for (int y = 0; y < CHUNK_HEIGHT; ++y)
        for (int z = 0; z < CHUNK_SIZE; ++z)
            for (int x = 0; x < CHUNK_SIZE; ++x)
                s.light[(y * CHUNK_SIZE + z) * CHUNK_SIZE + x] = c.packedLight(x, y, z);

    auto edge = [&](int ncx, int ncz, bool xEdge, int fixed,
                    std::vector<Block>& eb, std::vector<uint8_t>& el) {
        Chunk* n = getChunk(ncx, ncz);
        if (!n) return; // empty = treat as air / sky-lit
        eb.resize(size_t(CHUNK_HEIGHT) * CHUNK_SIZE);
        el.resize(eb.size());
        for (int y = 0; y < CHUNK_HEIGHT; ++y)
            for (int i = 0; i < CHUNK_SIZE; ++i) {
                eb[y * CHUNK_SIZE + i] = xEdge ? n->get(fixed, y, i) : n->get(i, y, fixed);
                el[y * CHUNK_SIZE + i] = xEdge ? n->packedLight(fixed, y, i)
                                               : n->packedLight(i, y, fixed);
            }
    };
    edge(c.cx() - 1, c.cz(), true, CHUNK_SIZE - 1, s.edgeXn, s.lightXn);
    edge(c.cx() + 1, c.cz(), true, 0,              s.edgeXp, s.lightXp);
    edge(c.cx(), c.cz() - 1, false, CHUNK_SIZE - 1, s.edgeZn, s.lightZn);
    edge(c.cx(), c.cz() + 1, false, 0,              s.edgeZp, s.lightZp);

    // Diagonal corner columns for AO/smooth lighting at chunk corners.
    auto corner = [&](int ncx, int ncz, int fx, int fz,
                      std::vector<Block>& cb, std::vector<uint8_t>& cl) {
        Chunk* n = getChunk(ncx, ncz);
        if (!n) return; // empty = treat as air / sky-lit
        cb.resize(CHUNK_HEIGHT);
        cl.resize(CHUNK_HEIGHT);
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            cb[y] = n->get(fx, y, fz);
            cl[y] = n->packedLight(fx, y, fz);
        }
    };
    constexpr int E = CHUNK_SIZE - 1;
    corner(c.cx() - 1, c.cz() - 1, E, E, s.cornerXnZn, s.cornerLightXnZn);
    corner(c.cx() + 1, c.cz() - 1, 0, E, s.cornerXpZn, s.cornerLightXpZn);
    corner(c.cx() - 1, c.cz() + 1, E, 0, s.cornerXnZp, s.cornerLightXnZp);
    corner(c.cx() + 1, c.cz() + 1, 0, 0, s.cornerXpZp, s.cornerLightXpZp);
    return s;
}

void World::processMeshing(int enqueueBudget, const glm::vec3& playerPos,
                           const Frustum* frustum, float uploadBudgetMs) {
    int pcx = floorDiv((int)std::floor(playerPos.x), CHUNK_SIZE);
    int pcz = floorDiv((int)std::floor(playerPos.z), CHUNK_SIZE);
    // Priority: chunks the camera can see first, then nearest-first.
    auto priority = [&](const ChunkKey& k) {
        int dx = k.x - pcx, dz = k.z - pcz;
        int d2 = dx * dx + dz * dz;
        if (!frustum) return d2;
        glm::vec3 mn(float(k.x * CHUNK_SIZE), 0.0f, float(k.z * CHUNK_SIZE));
        glm::vec3 mx = mn + glm::vec3(CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE);
        return frustum->intersectsAABB(mn, mx) ? d2 : d2 + 1000000;
    };

    // 1. Collect finished meshes into the upload queue. The chunk's pipeline
    //    slot frees immediately (a chunk edited while its mesh was in flight
    //    becomes dirty again and can re-enqueue right away), but the GL
    //    upload itself waits for a budget slot below. One entry per chunk:
    //    a newer result replaces a queued older one.
    std::vector<std::pair<ChunkKey, MeshData>> done;
    {
        std::lock_guard<std::mutex> l(meshM_);
        done.swap(meshDone_);
    }
    for (auto& [key, md] : done) {
        --meshInFlight_;
        auto it = chunks_.find(key);
        if (it == chunks_.end()) continue; // unloaded while meshing
        it->second->meshInFlight = false;
        bool replaced = false;
        for (auto& q : uploadQueue_)
            if (q.first == key) { q.second = std::move(md); replaced = true; break; }
        if (!replaced) uploadQueue_.emplace_back(key, std::move(md));
    }
    // Chunks can also unload while their upload waits in the queue.
    uploadQueue_.erase(
        std::remove_if(uploadQueue_.begin(), uploadQueue_.end(),
                       [&](const std::pair<ChunkKey, MeshData>& q) {
                           return !chunks_.count(q.first);
                       }),
        uploadQueue_.end());

    // 2. Upload within the frame budget, best priority first; always at
    //    least one per frame so the queue drains even under a tiny budget.
    std::sort(uploadQueue_.begin(), uploadQueue_.end(),
              [&](const std::pair<ChunkKey, MeshData>& a,
                  const std::pair<ChunkKey, MeshData>& b) {
                  return priority(a.first) < priority(b.first);
              });
    uploads_ = 0;
    auto t0 = Clock::now();
    size_t did = 0;
    for (; did < uploadQueue_.size(); ++did) {
        if (did > 0 && msSince(t0) > uploadBudgetMs) break;
        chunks_.at(uploadQueue_[did].first)->uploadMesh(uploadQueue_[did].second);
        ++uploads_;
    }
    uploadQueue_.erase(uploadQueue_.begin(), uploadQueue_.begin() + did);

    // 3. Enqueue dirty chunks for the workers, best priority first, so an
    //    edit or light change next to the player never waits behind far
    //    chunks streaming in.
    std::vector<std::pair<int, Chunk*>> dirty;
    for (auto& [key, chunk] : chunks_)
        if (chunk->dirty && !chunk->meshInFlight)
            dirty.push_back({priority(key), chunk.get()});
    if ((int)dirty.size() > enqueueBudget)
        std::partial_sort(dirty.begin(), dirty.begin() + enqueueBudget, dirty.end());
    for (int i = 0; i < (int)dirty.size() && i < enqueueBudget; ++i) {
        Chunk* chunk = dirty[i].second;
        chunk->dirty = false;
        chunk->meshInFlight = true;
        ++meshInFlight_;
        auto snap = std::make_shared<ChunkSnapshot>(snapshot(*chunk));
        ChunkKey k{chunk->cx(), chunk->cz()};
        pool_.push([this, k, snap] {
            auto t0 = Clock::now();
            MeshData md = buildMeshData(*snap);
            ema(meshMs_, msSince(t0));
            std::lock_guard<std::mutex> l(meshM_);
            meshDone_.emplace_back(k, std::move(md));
        });
    }
}

bool World::isAreaReady(const glm::vec3& pos, int radius) const {
    int pcx = floorDiv((int)std::floor(pos.x), CHUNK_SIZE);
    int pcz = floorDiv((int)std::floor(pos.z), CHUNK_SIZE);
    for (int dz = -radius; dz <= radius; ++dz)
        for (int dx = -radius; dx <= radius; ++dx)
            if (!getChunk(pcx + dx, pcz + dz)) return false;
    return true;
}

void World::waitUntilLoaded(const glm::vec3& pos, int radius, int timeoutMs) {
    auto t0 = Clock::now();
    while (!isAreaReady(pos, radius)) {
        update(pos, std::max(radius, 2));
        if (msSince(t0) > (float)timeoutMs) {
            std::fprintf(stderr, "warning: world load timed out\n");
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

namespace {
// Visible chunks ordered by distance to the eye: front-to-back for the
// opaque pass (early-z rejects hidden fragments), back-to-front for the
// translucent water pass (correct blending).
struct DrawItem { float dist2; Chunk* chunk; float ox, oz; };

std::vector<DrawItem> visibleSorted(
    const std::unordered_map<ChunkKey, std::unique_ptr<Chunk>, ChunkKeyHash>& chunks,
    const Frustum& frustum, const glm::vec3& eye, bool frontToBack) {
    std::vector<DrawItem> items;
    items.reserve(chunks.size());
    for (auto& [key, chunk] : chunks) {
        glm::vec3 mn(float(key.x * CHUNK_SIZE), 0.0f, float(key.z * CHUNK_SIZE));
        glm::vec3 mx = mn + glm::vec3(CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE);
        if (!frustum.intersectsAABB(mn, mx)) continue;
        glm::vec2 center(mn.x + CHUNK_SIZE * 0.5f, mn.z + CHUNK_SIZE * 0.5f);
        glm::vec2 d = center - glm::vec2(eye.x, eye.z);
        items.push_back({glm::dot(d, d), chunk.get(), mn.x, mn.z});
    }
    std::sort(items.begin(), items.end(), [&](const DrawItem& a, const DrawItem& b) {
        return frontToBack ? a.dist2 < b.dist2 : a.dist2 > b.dist2;
    });
    return items;
}
}

void World::drawChunks(const Frustum& frustum, const glm::vec3& eye, int originLoc) {
    drawn_ = 0;
    for (const DrawItem& it : visibleSorted(chunks_, frustum, eye, true)) {
        glUniform3f(originLoc, it.ox, 0.0f, it.oz);
        it.chunk->draw();
        ++drawn_;
    }
}

void World::drawWater(const Frustum& frustum, const glm::vec3& eye, int originLoc) {
    for (const DrawItem& it : visibleSorted(chunks_, frustum, eye, false)) {
        glUniform3f(originLoc, it.ox, 0.0f, it.oz);
        it.chunk->drawWater();
    }
}

RaycastHit World::raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist) const {
    // Amanatides & Woo voxel traversal.
    RaycastHit out;
    glm::ivec3 pos((int)std::floor(origin.x), (int)std::floor(origin.y), (int)std::floor(origin.z));
    glm::ivec3 step(dir.x > 0 ? 1 : -1, dir.y > 0 ? 1 : -1, dir.z > 0 ? 1 : -1);

    auto safeInv = [](float v) { return v != 0.0f ? 1.0f / v : 1e30f; };
    glm::vec3 invDir(safeInv(dir.x), safeInv(dir.y), safeInv(dir.z));
    glm::vec3 tDelta(std::abs(invDir.x), std::abs(invDir.y), std::abs(invDir.z));

    auto boundary = [](float o, int p, int s) { return s > 0 ? float(p + 1) - o : o - float(p); };
    glm::vec3 tMax(boundary(origin.x, pos.x, step.x) * tDelta.x,
                   boundary(origin.y, pos.y, step.y) * tDelta.y,
                   boundary(origin.z, pos.z, step.z) * tDelta.z);

    glm::ivec3 prev = pos;
    float t = 0.0f;
    while (t <= maxDist) {
        if (isSolid(getBlock(pos.x, pos.y, pos.z))) {
            out.hit = true;
            out.block = pos;
            out.adjacent = prev;
            return out;
        }
        prev = pos;
        if (tMax.x < tMax.y && tMax.x < tMax.z) { t = tMax.x; pos.x += step.x; tMax.x += tDelta.x; }
        else if (tMax.y < tMax.z)               { t = tMax.y; pos.y += step.y; tMax.y += tDelta.y; }
        else                                    { t = tMax.z; pos.z += step.z; tMax.z += tDelta.z; }
    }
    return out;
}

void World::saveAllModified() {
    for (auto& [key, chunk] : chunks_) {
        if (chunk->modified) {
            saveChunk(*chunk);
            chunk->modified = false;
        }
    }
    saveLevel(); // keeps the day clock current (cheap: 16 bytes, atomic)
}

WorldStats World::stats() const {
    WorldStats s;
    s.loaded = (int)chunks_.size();
    s.drawn = drawn_;
    s.uploads = uploads_;
    s.genQueued = (int)pendingGen_.size();
    s.meshQueued = meshInFlight_;
    s.uploadQueued = (int)uploadQueue_.size();
    s.genMs = genMs_.load(std::memory_order_relaxed);
    s.meshMs = meshMs_.load(std::memory_order_relaxed);
    return s;
}
