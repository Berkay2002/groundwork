#include "world/World.h"
#include "render/GLCompat.h"
#include "world/Lighting.h"
#include "world/WorldSave.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <utility>

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

bool sameFrustum(const Frustum& a, const Frustum& b) {
    for (int i = 0; i < 6; ++i) {
        glm::vec4 d = a.planes[i] - b.planes[i];
        if (std::abs(d.x) > 1e-5f || std::abs(d.y) > 1e-5f ||
            std::abs(d.z) > 1e-5f || std::abs(d.w) > 1e-3f) {
            return false;
        }
    }
    return true;
}
}

class World::LightingAccess : public lighting::Accessor {
public:
    explicit LightingAccess(World& w) : read_(w), write_(&w) {}
    explicit LightingAccess(const World& w) : read_(w), write_(nullptr) {}

    bool hasChunkAt(int wx, int wz) const override {
        return read_.getChunk(World::floorDiv(wx, CHUNK_SIZE),
                              World::floorDiv(wz, CHUNK_SIZE)) != nullptr;
    }
    Block blockAt(int wx, int wy, int wz) const override {
        return read_.getBlock(wx, wy, wz);
    }
    uint8_t cellLightAt(lighting::Channel ch, int wx, int wy, int wz) const override {
        Chunk* c = read_.getChunk(World::floorDiv(wx, CHUNK_SIZE),
                                  World::floorDiv(wz, CHUNK_SIZE));
        if (!c || wy < 0 || wy >= CHUNK_HEIGHT) return 0;
        int lx = World::mod(wx, CHUNK_SIZE), lz = World::mod(wz, CHUNK_SIZE);
        return ch == lighting::Channel::Sun ? c->sunLight(lx, wy, lz)
                                            : c->blockLight(lx, wy, lz);
    }
    void setCellLight(lighting::Channel ch, int wx, int wy, int wz, uint8_t v) override {
        if (!write_ || wy < 0 || wy >= CHUNK_HEIGHT) return;
        int cx = World::floorDiv(wx, CHUNK_SIZE), cz = World::floorDiv(wz, CHUNK_SIZE);
        Chunk* c = write_->getChunk(cx, cz);
        if (!c) return;
        int lx = World::mod(wx, CHUNK_SIZE), lz = World::mod(wz, CHUNK_SIZE);
        if (ch == lighting::Channel::Sun) c->setSunLight(lx, wy, lz, v);
        else                              c->setBlockLight(lx, wy, lz, v);
        write_->markDirty(*c);
        write_->markBorderDirty(cx, cz, lx, lz);
    }

private:
    const World& read_;
    World* write_;
};

World::World(uint32_t seed, std::string saveDir, bool demoMode)
    : demoMode_(demoMode), seed_(loadOrCreateSeed(saveDir, seed, demoMode)),
      terrain_(seed_), saveDir_(std::move(saveDir)), pool_(workerCount()) {
    loadDayTime();
    loadBlockEntitiesFile(saveDir_ + "/block_entities.bin", blockEntities_);
}

// The seed lives in level.bin so a save directory stays valid even if the
// caller's default seed changes (unmodified chunks regenerate from the seed).
// Missing or corrupt file: adopt the fallback and (re)write it. Old versions
// migrate — rewriting them with the fallback would silently swap the
// terrain under an existing save.
uint32_t World::loadOrCreateSeed(const std::string& saveDir, uint32_t fallback,
                                 bool demoMode) {
    if (!demoMode) std::filesystem::create_directories(saveDir);
    std::string path = saveDir + "/level.bin";
    bool existed = std::filesystem::exists(path);
    worldsave::LevelFile level = worldsave::loadLevelFile(path);
    if (level.ok) return level.seed;
    if (existed) {
        std::fprintf(stderr, "warning: bad level.bin, rewriting with seed %u\n",
                     fallback);
    }
    // Demo runs still adopt the fallback seed but never write it back.
    if (!demoMode && !worldsave::saveLevelFile(path, fallback, 0.0f))
        std::fprintf(stderr, "warning: failed to write level.bin\n");
    return fallback;
}

void World::loadDayTime() {
    worldsave::LevelFile level = worldsave::loadLevelFile(saveDir_ + "/level.bin");
    if (level.ok) dayTime_ = level.dayTime;
}

void World::saveLevel() const {
    if (!worldsave::saveLevelFile(saveDir_ + "/level.bin", seed_, dayTime_))
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
    // Lit<->unlit furnace swaps keep the block entity; anything else does not.
    if (isFurnaceBlock(old) && !isFurnaceBlock(b))
        blockEntities_.removeFurnace({wx, wy, wz});
    c->set(lx, wy, lz, b);
    markDirty(*c);
    c->modified = true;
    // Border block changes affect the neighbor's mesh too.
    markBorderDirty(cx, cz, lx, lz);

    LightingAccess light(*this);
    lighting::onBlockChanged(light, old, b, glm::ivec3(wx, wy, wz));

    // Any edit can start, feed, or cut off a flow: queue whatever water
    // is in or around the changed cell for the next fluid tick.
    scheduleFluidAround(wx, wy, wz);
}

uint8_t World::sunLightAt(int wx, int wy, int wz) const {
    LightingAccess light(*this);
    return lighting::lightAt(light, lighting::Channel::Sun, wx, wy, wz);
}
uint8_t World::blockLightAt(int wx, int wy, int wz) const {
    LightingAccess light(*this);
    return lighting::lightAt(light, lighting::Channel::Block, wx, wy, wz);
}

// A changed border cell invalidates the meshes that can see it: the face
// neighbor(s), and — because AO/smooth lighting sample diagonally — the
// diagonal neighbor when the cell sits on a chunk corner.
void World::markBorderDirty(int cx, int cz, int lx, int lz) {
    bool xn = lx == 0, xp = lx == CHUNK_SIZE - 1;
    bool zn = lz == 0, zp = lz == CHUNK_SIZE - 1;
    auto mark = [&](int dx, int dz) {
        if (Chunk* n = getChunk(cx + dx, cz + dz)) markDirty(*n);
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

void World::markNeighborsDirty(int cx, int cz) {
    // Diagonals included: a new chunk's corner columns feed the AO/smooth
    // lighting of all eight surrounding meshes.
    static const int d[8][2] = {{1,0},{-1,0},{0,1},{0,-1},
                                {1,1},{1,-1},{-1,1},{-1,-1}};
    for (auto& o : d)
        if (Chunk* n = getChunk(cx + o[0], cz + o[1])) markDirty(*n);
}

void World::markDirty(Chunk& c) {
    c.dirty = true;
    if (!c.queuedDirty) {
        c.queuedDirty = true;
        dirtyQueue_.push_back({c.cx(), c.cz()});
    }
}

bool World::loadChunkFromDisk(Chunk& c) const {
    std::string path = worldsave::chunkPath(saveDir_, c.cx(), c.cz());
    bool existed = std::filesystem::exists(path);
    bool ok = worldsave::loadChunkFile(path, c.rawData(), Chunk::rawSize());
    if (!ok && existed) {
        std::fprintf(stderr, "warning: chunk %d,%d has bad/old save format, regenerating\n",
                     c.cx(), c.cz());
    }
    return ok;
}

void World::saveChunk(const Chunk& c) {
    // Gate every chunk write, not just saveAllModified(): chunk eviction in
    // update() also lands here, and demo runs must never touch real saves.
    if (demoMode_) return;
    bool ok = worldsave::saveChunkFile(worldsave::chunkPath(saveDir_, c.cx(), c.cz()),
                                       c.rawData(), Chunk::rawSize());
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
        markNeighborsDirty(key.x, key.z); // their border faces may now be hidden
        Chunk* c = chunk.get();
        chunks_[key] = std::move(chunk);
        markDirty(*c); // fresh chunks arrive dirty; register them in the queue
        LightingAccess light(*this);
        lighting::onChunkAdded(light, key.x, key.z); // exchange light with neighbors
        seedFluidsFromChunk(key.x, key.z); // resume interrupted flows
        streamEvents_.loaded.push_back(key);
    }

    int pcx = floorDiv((int)std::floor(playerPos.x), CHUNK_SIZE);
    int pcz = floorDiv((int)std::floor(playerPos.z), CHUNK_SIZE);

    // Both sweeps below are O(renderDistance²); at large distances they cost
    // more than the rest of the frame, so they only re-run when something
    // they depend on changed (player chunk, distance, or the chunk set).
    const bool moved = pcx != lastPcx_ || pcz != lastPcz_ || renderDistance != lastRd_;
    lastPcx_ = pcx; lastPcz_ = pcz; lastRd_ = renderDistance;
    if (moved || !done.empty()) streamScanClean_ = false;

    // 2. Request missing chunks nearest-first, keeping a bounded backlog.
    if (!streamScanClean_) {
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
        // Budget left over ⇒ the ring loop ran to completion, so every cell
        // in range is now loaded or pending; nothing to find until the
        // player moves or a pending chunk arrives (both clear the flag).
        streamScanClean_ = budget > 0;
    }

    // 3. Unload far chunks (save modified ones first). The far set only
    //    changes when the player moves or a chunk arrives.
    if (moved || !done.empty()) {
        bool unloaded = false;
        for (auto it = chunks_.begin(); it != chunks_.end();) {
            int dx = std::abs(it->first.x - pcx), dz = std::abs(it->first.z - pcz);
            if (std::max(dx, dz) > renderDistance + 2) {
                if (it->second->modified) saveChunk(*it->second);
                streamEvents_.unloaded.push_back(it->first);
                removeDrawCandidate(it->first);
                it = chunks_.erase(it);
                unloaded = true;
            } else {
                ++it;
            }
        }
        if (unloaded) visibleCacheValid_ = false;
    }
}

ChunkStreamEvents World::consumeStreamEvents() {
    ChunkStreamEvents out;
    out.loaded.swap(streamEvents_.loaded);
    out.unloaded.swap(streamEvents_.unloaded);
    return out;
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
        const ChunkKey& key = uploadQueue_[did].first;
        Chunk* chunk = chunks_.at(key).get();
        chunk->uploadMesh(uploadQueue_[did].second);
        refreshDrawCandidate(key, *chunk);
        ++uploads_;
    }
    uploadQueue_.erase(uploadQueue_.begin(), uploadQueue_.begin() + did);
    if (uploads_ > 0) visibleCacheValid_ = false;

    // 3. Enqueue dirty chunks for the workers, best priority first, so an
    //    edit or light change next to the player never waits behind far
    //    chunks streaming in. Candidates come from dirtyQueue_ (every
    //    markDirty registers there once), not a scan of all loaded chunks.
    std::vector<std::pair<int, Chunk*>> dirty;
    std::vector<ChunkKey> keep; // still dirty but not dispatchable this frame
    for (const ChunkKey& key : dirtyQueue_) {
        auto it = chunks_.find(key);
        if (it == chunks_.end()) continue;        // unloaded while queued
        Chunk* chunk = it->second.get();
        if (!chunk->dirty) { chunk->queuedDirty = false; continue; }
        if (chunk->meshInFlight) { keep.push_back(key); continue; }
        dirty.push_back({priority(key), chunk});
    }
    if ((int)dirty.size() > enqueueBudget)
        std::partial_sort(dirty.begin(), dirty.begin() + enqueueBudget, dirty.end());
    for (int i = enqueueBudget; i < (int)dirty.size(); ++i) // over budget: retry next frame
        keep.push_back({dirty[i].second->cx(), dirty[i].second->cz()});
    dirtyQueue_.swap(keep);
    for (int i = 0; i < (int)dirty.size() && i < enqueueBudget; ++i) {
        Chunk* chunk = dirty[i].second;
        chunk->dirty = false;
        chunk->queuedDirty = false;
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

void World::refreshDrawCandidate(const ChunkKey& key, Chunk& chunk) {
    if (!chunk.hasOpaque() && !chunk.hasWater()) {
        removeDrawCandidate(key);
        return;
    }

    auto fill = [&](DrawCandidate& item) {
        const float ox = float(key.x * CHUNK_SIZE);
        const float oz = float(key.z * CHUNK_SIZE);
        item = {key, &chunk,
                {ox, 0.0f, oz},
                {ox + CHUNK_SIZE, float(CHUNK_HEIGHT), oz + CHUNK_SIZE},
                ox, oz,
                ox + CHUNK_SIZE * 0.5f, oz + CHUNK_SIZE * 0.5f};
    };

    auto it = drawCandidateIndex_.find(key);
    if (it == drawCandidateIndex_.end()) {
        DrawCandidate item{};
        fill(item);
        drawCandidateIndex_[key] = drawCandidates_.size();
        drawCandidates_.push_back(item);
    } else {
        fill(drawCandidates_[it->second]);
    }
}

void World::removeDrawCandidate(const ChunkKey& key) {
    auto it = drawCandidateIndex_.find(key);
    if (it == drawCandidateIndex_.end()) return;

    size_t idx = it->second;
    size_t last = drawCandidates_.size() - 1;
    if (idx != last) {
        drawCandidates_[idx] = drawCandidates_[last];
        drawCandidateIndex_[drawCandidates_[idx].key] = idx;
    }
    drawCandidates_.pop_back();
    drawCandidateIndex_.erase(it);
    visibleCacheValid_ = false;
}

// Cull the drawable chunk list once per frame. Chunks with nothing in either
// pass are kept out at mesh-upload time, so moving-camera rebuilds do not scan
// the whole chunk map or reconstruct chunk bounds.
void World::drawChunks(const Frustum& frustum, const glm::vec3& eye, int originLoc) {
    bool sameEye = glm::dot(eye - visibleCacheEye_, eye - visibleCacheEye_) < 1e-6f;
    if (!visibleCacheValid_ || !sameEye || !sameFrustum(frustum, visibleCacheFrustum_)) {
        visible_.clear();
        visible_.reserve(drawCandidates_.size() / 4);
        for (const DrawCandidate& item : drawCandidates_) {
            if (!frustum.intersectsAABB(item.mn, item.mx)) continue;
            float dx = item.centerX - eye.x;
            float dz = item.centerZ - eye.z;
            visible_.push_back({dx * dx + dz * dz, item.chunk, item.ox, item.oz});
        }
        visibleCacheEye_ = eye;
        visibleCacheFrustum_ = frustum;
        visibleCacheValid_ = true;
    }

    drawn_ = 0;
    for (const DrawItem& it : visible_) {
        if (!it.chunk->hasOpaque()) continue;
        glUniform3f(originLoc, it.ox, 0.0f, it.oz);
        it.chunk->draw();
        ++drawn_;
    }
}

void World::drawWater(const Frustum&, const glm::vec3&, int originLoc) {
    waterVisible_.clear();
    waterVisible_.reserve(visible_.size() / 8);
    for (const DrawItem& it : visible_) {
        if (it.chunk->hasWater()) waterVisible_.push_back(it);
    }
    std::sort(waterVisible_.begin(), waterVisible_.end(),
              [](const DrawItem& a, const DrawItem& b) { return a.dist2 > b.dist2; });
    for (const DrawItem& it : waterVisible_) {
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

    // Torches don't fill their voxel cell: the targetable shape is the thin
    // post, so a ray that crosses the cell but misses the post keeps going
    // (Minecraft-style per-block selection shapes).
    auto hitsTorchPost = [&](glm::ivec3 cell) {
        glm::vec3 lo = glm::vec3(cell) + glm::vec3(TORCH_BOX_MIN, 0.0f, TORCH_BOX_MIN);
        glm::vec3 hi = glm::vec3(cell) + glm::vec3(TORCH_BOX_MAX, TORCH_BOX_TOP, TORCH_BOX_MAX);
        float t0 = 0.0f, t1 = maxDist;
        for (int a = 0; a < 3; ++a) {
            float ta = (lo[a] - origin[a]) * invDir[a];
            float tb = (hi[a] - origin[a]) * invDir[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
        return true;
    };

    glm::ivec3 prev = pos;
    float t = 0.0f;
    while (t <= maxDist) {
        Block b = getBlock(pos.x, pos.y, pos.z);
        if (isSolid(b) && (b != Block::Torch || hitsTorchPost(pos))) {
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
    if (demoMode_) return; // demo runs must not touch the real saves directory
    for (auto& [key, chunk] : chunks_) {
        if (chunk->modified) {
            saveChunk(*chunk);
            chunk->modified = false;
        }
    }
    saveLevel(); // keeps the day clock current (cheap: 16 bytes, atomic)
    if (!saveBlockEntitiesFile(saveDir_ + "/block_entities.bin", blockEntities_))
        std::fprintf(stderr, "warning: failed to write block_entities.bin\n");
}

FurnaceState& World::getOrCreateFurnace(glm::ivec3 pos) {
    return blockEntities_.getOrCreateFurnace(pos);
}

FurnaceState* World::furnaceAt(glm::ivec3 pos) {
    return blockEntities_.furnaceAt(pos);
}

const FurnaceState* World::furnaceAt(glm::ivec3 pos) const {
    return blockEntities_.furnaceAt(pos);
}

std::vector<ItemStack> World::takeFurnaceContents(glm::ivec3 pos) {
    std::vector<ItemStack> out;
    FurnaceState* f = blockEntities_.furnaceAt(pos);
    if (!f) return out;
    if (!f->input.empty()) out.push_back(f->input);
    if (!f->fuel.empty()) out.push_back(f->fuel);
    if (!f->output.empty()) out.push_back(f->output);
    blockEntities_.removeFurnace(pos);
    return out;
}

// --- Water simulation -------------------------------------------------
// Minecraft-style cellular water, main thread only, queue-driven: setBlock
// schedules the touched cells, tickFluids() re-evaluates them every
// FLUID_TICK_INTERVAL game ticks. Generated lakes are all source blocks on
// solid ground and are never queued, so still water costs nothing.

void World::scheduleFluid(int wx, int wy, int wz) {
    if (wy < 0 || wy >= CHUNK_HEIGHT) return;
    if (!isWater(getBlock(wx, wy, wz))) return;
    // Injective packing for |x|,|z| < 2^23: x bits 40..63, z 16..39, y 0..15.
    int64_t key = (int64_t(wx + (1 << 23)) << 40) |
                  (int64_t(wz + (1 << 23)) << 16) | wy;
    if (fluidPending_.insert(key).second)
        fluidQueue_.push_back({wx, wy, wz});
}

void World::scheduleFluidAround(int wx, int wy, int wz) {
    scheduleFluid(wx, wy, wz);
    scheduleFluid(wx + 1, wy, wz);
    scheduleFluid(wx - 1, wy, wz);
    scheduleFluid(wx, wy + 1, wz);
    scheduleFluid(wx, wy - 1, wz);
    scheduleFluid(wx, wy, wz + 1);
    scheduleFluid(wx, wy, wz - 1);
}

// Resume interrupted flows when a chunk enters memory (fresh or loaded from
// disk): flowing cells only. Border *sources* are deliberately not seeded —
// a pristine lake must not start draining into a cave just because the
// neighbor chunk streamed in; sources only move after a player edit.
void World::seedFluidsFromChunk(int cx, int cz) {
    Chunk* c = getChunk(cx, cz);
    if (!c) return;
    const int bx = cx * CHUNK_SIZE, bz = cz * CHUNK_SIZE;
    for (int y = 0; y < CHUNK_HEIGHT; ++y)
        for (int z = 0; z < CHUNK_SIZE; ++z)
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                Block b = c->get(x, y, z);
                if (isWater(b) && b != Block::Water)
                    scheduleFluid(bx + x, y, bz + z);
            }
    // Flows in loaded neighbors right at this chunk's border may have been
    // waiting for it to load before they could continue.
    for (int y = 0; y < CHUNK_HEIGHT; ++y)
        for (int i = 0; i < CHUNK_SIZE; ++i) {
            const int probes[4][2] = {{bx - 1, bz + i},
                                      {bx + CHUNK_SIZE, bz + i},
                                      {bx + i, bz - 1},
                                      {bx + i, bz + CHUNK_SIZE}};
            for (auto& p : probes) {
                Block b = getBlock(p[0], y, p[1]);
                if (isWater(b) && b != Block::Water)
                    scheduleFluid(p[0], y, p[1]);
            }
        }
}

// Minecraft's drop-seeking: spreading water prefers the direction(s) whose
// nearest hole (a cell it could fall into) is closest within 4 blocks,
// searching through cells water could occupy. No hole anywhere -> all
// open directions. Returns a bitmask over the H[] direction order.
int World::fluidSpreadMask(int wx, int wy, int wz) const {
    static const int H[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    constexpr int R = 4;             // MC's seek radius
    constexpr int G = 2 * R + 1;     // local grid side (9x9 around the cell)
    auto enterable = [&](int x, int z) {
        Block b = getBlock(x, wy, z);
        return b == Block::Air || isWater(b);
    };
    auto isHole = [&](int x, int z) {
        Block below = getBlock(x, wy - 1, z);
        return below == Block::Air ||
               (isWater(below) && below != Block::Water);
    };
    int dist[4];
    int best = INT_MAX;
    for (int d = 0; d < 4; ++d) {
        dist[d] = INT_MAX;
        int sx = wx + H[d][0], sz = wz + H[d][1];
        if (!enterable(sx, sz)) continue;
        // Breadth-first flood from the first step, capped at R steps.
        bool seen[G * G] = {};
        glm::ivec2 q[G * G];
        int qd[G * G];
        int head = 0, tail = 0;
        q[tail] = {sx, sz};
        qd[tail++] = 1;
        seen[(sz - wz + R) * G + (sx - wx + R)] = true;
        while (head < tail) {
            glm::ivec2 p = q[head];
            int pd = qd[head++];
            if (wy > 0 && isHole(p.x, p.y)) { dist[d] = pd; break; }
            if (pd == R) continue;
            for (auto& h : H) {
                int nx = p.x + h[0], nz = p.y + h[1];
                int gx = nx - wx + R, gz = nz - wz + R;
                if (gx < 0 || gx >= G || gz < 0 || gz >= G) continue;
                if (seen[gz * G + gx] || !enterable(nx, nz)) continue;
                seen[gz * G + gx] = true;
                q[tail] = {nx, nz};
                qd[tail++] = pd + 1;
            }
        }
        best = std::min(best, dist[d]);
    }
    int mask = 0;
    for (int d = 0; d < 4; ++d) {
        if (best == INT_MAX ? enterable(wx + H[d][0], wz + H[d][1])
                            : dist[d] == best)
            mask |= 1 << d;
    }
    return mask;
}

void World::updateFluidCell(int wx, int wy, int wz) {
    static const int H[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    Block b = getBlock(wx, wy, wz);
    if (!isWater(b)) return;

    Block below = wy > 0 ? getBlock(wx, wy - 1, wz) : Block::Bedrock;
    // 1. Flowing cells re-derive their level from what still feeds them —
    //    this is what makes water drain away when its source is removed.
    if (b != Block::Water) {
        int sources = 0, feed = 0;
        for (auto& h : H) {
            Block n = getBlock(wx + h[0], wy, wz + h[1]);
            if (n == Block::Water) ++sources;
            feed = std::max(feed, waterLevel(n));
        }
        bool supported = isCollidable(below) || below == Block::Water;
        Block want;
        if (sources >= 2 && supported) {
            want = Block::Water; // infinite-source rule: 2x2 pools refill
        } else if (isWater(getBlock(wx, wy + 1, wz))) {
            want = Block::WaterFall; // fed from above: full falling column
        } else {
            int lvl = std::min(feed - 1, 7);
            want = lvl >= 1 ? waterFlowBlock(lvl) : Block::Air;
        }
        if (want != b) {
            setBlock(wx, wy, wz, want); // schedules the neighborhood
            if (!isWater(want)) return;
            b = want;
        }
    }

    // 2. Spread: down first; only water that can't fall creeps sideways.
    bool belowEnterable =
        below == Block::Air ||
        (isWater(below) && below != Block::Water && below != Block::WaterFall);
    if (wy > 0 && belowEnterable) {
        setBlock(wx, wy - 1, wz, Block::WaterFall);
        return;
    }
    if (!(isCollidable(below) || below == Block::Water))
        return; // mid-air falling column: keeps falling, never spreads
    int out = std::min(waterLevel(b) - 1, 7);
    if (out < 1) return;
    int mask = fluidSpreadMask(wx, wy, wz);
    for (int d = 0; d < 4; ++d) {
        if (!(mask & (1 << d))) continue;
        int nx = wx + H[d][0], nz = wz + H[d][1];
        Block n = getBlock(nx, wy, nz);
        bool can = n == Block::Air ||
                   (isWater(n) && n != Block::Water && waterLevel(n) < out);
        if (can) setBlock(nx, wy, nz, waterFlowBlock(out));
    }
}

void World::tickFluids() {
    if (++fluidTickCounter_ < FLUID_TICK_INTERVAL) return;
    fluidTickCounter_ = 0;
    if (fluidQueue_.empty()) return;
    // Swap the queue out: cells touched while processing land in the fresh
    // queue and run on the NEXT fluid tick, which is what paces the flow.
    std::vector<glm::ivec3> cells;
    cells.swap(fluidQueue_);
    fluidPending_.clear();
    for (const auto& p : cells) updateFluidCell(p.x, p.y, p.z);
}

void World::tickBlockEntities() {
    blockEntities_.tickFurnaces();
    // Sync the visual lit state to the burn state. setBlock handles light
    // (the lit row emits) and remeshing; the furnace-to-furnace swap keeps
    // the block entity. Unloaded chunks read Air and are skipped.
    for (const auto& [pos, f] : blockEntities_.furnaces()) {
        Block cur = getBlock(pos.x, pos.y, pos.z);
        if (!isFurnaceBlock(cur)) continue;
        Block want = f.burnTicksRemaining > 0 ? furnaceLitVariant(cur)
                                              : furnaceUnlitVariant(cur);
        if (cur != want) setBlock(pos.x, pos.y, pos.z, want);
    }
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
