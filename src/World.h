#pragma once
#include "Chunk.h"
#include "Frustum.h"
#include "JobQueue.h"
#include "Terrain.h"
#include <atomic>
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

// Chunk lifecycle: missing -> pendingGen (worker generates/loads) ->
// loaded+dirty -> meshInFlight (worker builds vertices) -> uploaded.
// Edits set dirty again; unload saves modified chunks.
// All chunk-map mutation and GL work happens on the main thread; workers
// only see freshly created chunks or immutable snapshots.
class World {
public:
    World(uint32_t seed, std::string saveDir);
    ~World();

    Block getBlock(int wx, int wy, int wz) const;
    void setBlock(int wx, int wy, int wz, Block b); // marks chunks dirty + modified, relights

    // Light queries (0..15). Above the world: full sun; missing chunk: 0.
    uint8_t sunLightAt(int wx, int wy, int wz) const;
    uint8_t blockLightAt(int wx, int wy, int wz) const;

    // Streaming: integrate finished generation jobs, request missing chunks
    // around the player, unload distant ones.
    void update(const glm::vec3& playerPos, int renderDistance);

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
    // each draw sets the chunk origin via the given uniform location.
    void drawChunks(const Frustum& frustum, const glm::vec3& eye, int originLoc);
    // Translucent water pass, back-to-front; call after drawChunks with
    // blending enabled.
    void drawWater(const Frustum& frustum, const glm::vec3& eye, int originLoc);

    RaycastHit raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist) const;

    void saveAllModified();
    WorldStats stats() const;

    // The world's actual seed: read from saves/<dir>/level.bin when present
    // (written on world creation), so a save stays valid even if the
    // default seed constant in main.cpp changes.
    uint32_t seed() const { return seed_; }

    static int floorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
    static int mod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }

private:
    static uint32_t loadOrCreateSeed(const std::string& saveDir, uint32_t fallback);
    Chunk* getChunk(int cx, int cz) const;
    void saveChunk(const Chunk& c);
    bool loadChunkFromDisk(Chunk& c) const;
    std::string chunkPath(int cx, int cz) const;
    void markNeighborsDirty(int cx, int cz);
    void markBorderDirty(int cx, int cz, int lx, int lz);
    ChunkSnapshot snapshot(const Chunk& c) const;

    // Cross-chunk light BFS (main thread only: walks live chunks, marks them
    // dirty). Workers compute per-chunk initial light; everything that can
    // cross a border goes through these.
    enum class LightChan { Sun, Block };
    uint8_t getLight(LightChan ch, int wx, int wy, int wz) const;
    void setLight(LightChan ch, int wx, int wy, int wz, uint8_t v);
    void addLight(LightChan ch, std::vector<glm::ivec3> seeds);
    void removeLight(LightChan ch, const glm::ivec3& pos);
    void seedChunkBorderLight(int cx, int cz);

    uint32_t seed_;
    Terrain terrain_;
    std::string saveDir_;
    std::unordered_map<ChunkKey, std::unique_ptr<Chunk>, ChunkKeyHash> chunks_;

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

    // Perf counters (workers store, main thread reads).
    std::atomic<float> genMs_{0.0f}, meshMs_{0.0f};
    int drawn_ = 0, uploads_ = 0;

    // Declared last: destroyed (joined) first, so workers can't touch
    // queues or terrain after they're gone.
    JobQueue pool_;
};
