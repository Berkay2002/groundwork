#pragma once
#include "world/Chunk.h"
#include "render/Frustum.h"
#include "platform/JobQueue.h"
#include "world/Terrain.h"
#include "world/BlockEntity.h"
#include <atomic>
#include <cstddef>
#include <climits>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ChunkKey {
    int x, z;
    bool operator==(const ChunkKey& o) const { return x == o.x && z == o.z; }
};
struct ChunkKeyHash {
    size_t operator()(const ChunkKey& k) const {
        return std::hash<int64_t>()((int64_t(k.x) << 32) ^ uint32_t(k.z));
    }
};

struct RaycastHit {
    bool hit = false;
    glm::ivec3 block{};   // the solid block that was hit
    glm::ivec3 adjacent{};// the air block in front of the hit face
};

struct WorldStats {
    int loaded = 0;       // chunks in memory
    int drawn = 0;        // chunks drawn last frame (after frustum culling)
    int uploads = 0;      // meshes uploaded last frame
    int genQueued = 0;    // generation jobs in flight
    int meshQueued = 0;   // mesh jobs in flight
    int uploadQueued = 0; // finished meshes still waiting for a GL upload slot
    float genMs = 0;      // moving average per-chunk generation time
    float meshMs = 0;     // moving average per-chunk mesh build time
};

struct ChunkStreamEvents {
    std::vector<ChunkKey> loaded;
    std::vector<ChunkKey> unloaded;
};

// Chunk lifecycle: missing -> pendingGen (worker generates/loads) ->
// loaded+dirty -> meshInFlight (worker builds vertices) -> uploaded.
// Edits set dirty again; unload saves modified chunks.
// All chunk-map mutation and GL work happens on the main thread; workers
// only see freshly created chunks or immutable snapshots.
class World {
public:
    // demoMode suppresses every disk write — including the constructor's
    // save-dir/level.bin creation — so demo runs never touch real saves.
    World(uint32_t seed, std::string saveDir, bool demoMode = false);
    ~World();

    Block getBlock(int wx, int wy, int wz) const;
    void setBlock(int wx, int wy, int wz, Block b); // marks chunks dirty + modified, relights

    // Light queries (0..15). Above the world: full sun; missing chunk: 0.
    uint8_t sunLightAt(int wx, int wy, int wz) const;
    uint8_t blockLightAt(int wx, int wy, int wz) const;

    // Streaming: integrate finished generation jobs, request missing chunks
    // around the player, unload distant ones.
    void update(const glm::vec3& playerPos, int renderDistance);
    ChunkStreamEvents consumeStreamEvents();

    // Mesh pipeline: upload finished meshes (GL!) within a per-frame time
    // budget — in-frustum and near chunks first, the rest carry over to the
    // next frame — then enqueue dirty chunks for the workers, also
    // nearest-first. At least one upload happens per frame so the queue
    // always drains.
    void processMeshing(int enqueueBudget, const glm::vec3& playerPos,
                        const Frustum* frustum, float uploadBudgetMs);

    // True when all chunks within `radius` of pos are in memory.
    bool isAreaReady(const glm::vec3& pos, int radius) const;
    // Pump update() until the area is ready (no GL needed; usable in tests).
    void waitUntilLoaded(const glm::vec3& pos, int radius, int timeoutMs);

    // Opaque pass, front-to-back for early-z. Vertices are chunk-local, so
    // each draw sets the chunk origin via the given uniform location. Also
    // builds the frame's visible-chunk list that drawWater reuses.
    void drawChunks(const Frustum& frustum, const glm::vec3& eye, int originLoc);
    // Translucent water pass; call after drawChunks with blending enabled.
    // Walks drawChunks' visible list in reverse (back-to-front).
    void drawWater(const Frustum& frustum, const glm::vec3& eye, int originLoc);

    RaycastHit raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist) const;

    void saveAllModified();
    WorldStats stats() const;

    // Minecraft-style water spreading, main thread only. Call once per game
    // tick; cells actually re-evaluate every FLUID_TICK_INTERVAL ticks
    // (Minecraft's water rate). Only cells touched by setBlock (or resumed
    // flows on chunk load) are queued — stable water costs nothing.
    static constexpr int FLUID_TICK_INTERVAL = 5;
    void tickFluids();
    size_t fluidQueueSize() const { return fluidQueue_.size(); }

    FurnaceState& getOrCreateFurnace(glm::ivec3 pos);
    FurnaceState* furnaceAt(glm::ivec3 pos);
    const FurnaceState* furnaceAt(glm::ivec3 pos) const;
    std::vector<ItemStack> takeFurnaceContents(glm::ivec3 pos);
    void tickBlockEntities();

    // The world's actual seed: read from saves/<dir>/level.bin when present
    // (written on world creation), so a save stays valid even if the
    // default seed constant in main.cpp changes.
    uint32_t seed() const { return seed_; }

    // World day clock in seconds (drives the day/night cycle); persisted in
    // level.bin (v2) by saveAllModified so a save resumes at its time of day.
    float dayTime() const { return dayTime_; }
    void setDayTime(float t) { dayTime_ = t; }

    static int floorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
    static int mod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }

private:
    class LightingAccess;
    static uint32_t loadOrCreateSeed(const std::string& saveDir,
                                     uint32_t fallback, bool demoMode);
    void loadDayTime();
    void saveLevel() const;
    Chunk* getChunk(int cx, int cz) const;
    void saveChunk(const Chunk& c);
    bool loadChunkFromDisk(Chunk& c) const;
    void markNeighborsDirty(int cx, int cz);
    void markBorderDirty(int cx, int cz, int lx, int lz);
    // The single way a chunk becomes dirty: sets the flag and registers the
    // chunk in dirtyQueue_ so processMeshing never scans the whole map.
    void markDirty(Chunk& c);
    ChunkSnapshot snapshot(const Chunk& c) const;

    // Fluid simulation internals (see tickFluids).
    void scheduleFluid(int wx, int wy, int wz);
    void scheduleFluidAround(int wx, int wy, int wz);
    void seedFluidsFromChunk(int cx, int cz);
    void updateFluidCell(int wx, int wy, int wz);
    int fluidSpreadMask(int wx, int wy, int wz) const;

    bool demoMode_ = false; // if true, all save operations are suppressed
    uint32_t seed_;
    float dayTime_ = 0.0f; // seconds; 0 = morning
    Terrain terrain_;
    std::string saveDir_;
    std::unordered_map<ChunkKey, std::unique_ptr<Chunk>, ChunkKeyHash> chunks_;
    BlockEntityStore blockEntities_;
    ChunkStreamEvents streamEvents_;

    // Pending fluid cells (queue + dedupe set of packed positions).
    std::vector<glm::ivec3> fluidQueue_;
    std::unordered_set<int64_t> fluidPending_;
    int fluidTickCounter_ = 0;

    // Generation pipeline (pendingGen_ is main-thread only).
    std::unordered_set<ChunkKey, ChunkKeyHash> pendingGen_;
    std::mutex genM_;
    std::vector<std::pair<ChunkKey, std::unique_ptr<Chunk>>> genDone_;

    // Meshing pipeline.
    int meshInFlight_ = 0; // main-thread counter of jobs in the pipeline
    std::mutex meshM_;
    std::vector<std::pair<ChunkKey, MeshData>> meshDone_;
    // Finished meshes waiting for their budgeted GL upload (main thread
    // only). At most one entry per chunk: a newer result replaces the old.
    std::vector<std::pair<ChunkKey, MeshData>> uploadQueue_;
    // Chunks whose dirty flag is set (one entry per chunk, guarded by
    // Chunk::queuedDirty). Lets processMeshing touch only dirty chunks
    // instead of scanning all loaded ones every frame.
    std::vector<ChunkKey> dirtyQueue_;

    // Streaming-scan memo: the ring scan and unload sweep in update() are
    // O(renderDistance²); they only need to re-run when the player crosses
    // a chunk boundary, the distance changes, chunks were integrated, or
    // the last scan couldn't cover everything (request budget ran out).
    int lastPcx_ = INT_MIN, lastPcz_ = INT_MIN, lastRd_ = -1;
    bool streamScanClean_ = false;

    // Frame-visible chunks: built by drawChunks and reused by drawWater so
    // the chunk map is culled only once. Water chunks are sorted separately
    // back-to-front because the opaque pass does not need ordering.
    struct DrawCandidate {
        ChunkKey key;
        Chunk* chunk;
        glm::vec3 mn, mx;
        float ox, oz;
        float centerX, centerZ;
    };
    struct DrawItem { float dist2; Chunk* chunk; float ox, oz; };
    void refreshDrawCandidate(const ChunkKey& key, Chunk& chunk);
    void removeDrawCandidate(const ChunkKey& key);
    std::vector<DrawCandidate> drawCandidates_;
    std::unordered_map<ChunkKey, size_t, ChunkKeyHash> drawCandidateIndex_;
    std::vector<DrawItem> visible_;
    std::vector<DrawItem> waterVisible_;
    bool visibleCacheValid_ = false;
    glm::vec3 visibleCacheEye_{0.0f};
    Frustum visibleCacheFrustum_{};

    // Perf counters (workers store, main thread reads).
    std::atomic<float> genMs_{0.0f}, meshMs_{0.0f};
    int drawn_ = 0, uploads_ = 0;

    // Declared last: destroyed (joined) first, so workers can't touch
    // queues or terrain after they're gone.
    JobQueue pool_;
};
