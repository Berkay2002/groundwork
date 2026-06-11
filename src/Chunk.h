#pragma once
#include "Block.h"
#include <cstdint>
#include <vector>

constexpr int CHUNK_SIZE = 16;   // X and Z
constexpr int CHUNK_HEIGHT = 80; // Y

// Packed chunk vertex (14 bytes vs the old 24-byte float layout; 12 until
// day/night split brightness into sun/blk channels). Positions
// are chunk-local in 1/16-block units (so the torch's fractional geometry
// stays exact); the shader adds the chunk origin via a uniform. UVs are in
// 1/16-tile units and may span several tiles on greedy-merged faces — the
// block textures live in a GL texture array (one tile per layer, REPEAT
// wrapping), with `layer` selecting the tile.
// Sun and block light are baked as separate brightness channels so the
// day/night cycle can dim sunlight in the shader (light = max(sun *
// uSunLevel, blk)) without relighting or remeshing anything. Each channel
// already includes the face shade, smooth lighting, and AO factors.
struct ChunkVertex {
    uint16_t x, y, z;  // chunk-local position * 16
    uint16_t u, v;     // tile UV * 16 (wraps per tile on merged faces)
    uint8_t sun;       // sun-channel brightness * 255
    uint8_t blk;       // block-light-channel brightness * 255
    uint8_t layer;     // texture-array layer = atlas tile index
    uint8_t pad = 0;   // keep the stride even
};
static_assert(sizeof(ChunkVertex) == 14, "ChunkVertex must stay tightly packed");

// CPU-side mesh, built on any thread, uploaded on the GL thread.
// Water lives in its own arrays: it is drawn in a separate translucent pass
// after all opaque geometry.
struct MeshData {
    std::vector<ChunkVertex> verts;
    std::vector<uint32_t> inds;
    std::vector<ChunkVertex> waterVerts;
    std::vector<uint32_t> waterInds;
};

// Self-contained copy of everything mesh building needs: the chunk's blocks,
// one-block-deep slices of the four side neighbors, and the four diagonal
// corner columns (ambient occlusion / smooth lighting at a chunk-corner
// vertex samples diagonally). Workers read only this, never live chunks.
// Light bytes pack sunlight in the low nibble and block light in the high
// nibble; empty light vectors mean "fully sunlit" (manual test snapshots,
// missing neighbors).
struct ChunkSnapshot {
    int cx = 0, cz = 0;
    std::vector<Block> blocks;                       // [(y*CS+z)*CS+x]
    std::vector<Block> edgeXn, edgeXp, edgeZn, edgeZp; // [y*CS + (z or x)]
    std::vector<Block> cornerXnZn, cornerXpZn, cornerXnZp, cornerXpZp; // [y]
    std::vector<uint8_t> light;
    std::vector<uint8_t> lightXn, lightXp, lightZn, lightZp;
    std::vector<uint8_t> cornerLightXnZn, cornerLightXpZn,
                         cornerLightXnZp, cornerLightXpZp;
};

// Greedy mesher: coplanar faces of the same block merge into one quad only
// when their quantized corner AO/light tuples are identical, so merging never
// changes the shading. Pure function, GL-free, thread-safe.
MeshData buildMeshData(const ChunkSnapshot& s);

class Chunk {
public:
    Chunk(int cx, int cz);
    ~Chunk();

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    int cx() const { return cx_; }
    int cz() const { return cz_; }

    // Local coordinates, must be in range.
    Block get(int x, int y, int z) const {
        if (y < 0 || y >= CHUNK_HEIGHT) return Block::Air;
        return blocks_[index(x, y, z)];
    }
    void set(int x, int y, int z, Block b) {
        if (y < 0 || y >= CHUNK_HEIGHT) return;
        blocks_[index(x, y, z)] = b;
    }

    // Light access (sun = low nibble, block light = high nibble). Light is
    // never saved: it is recomputed on generation/load and patched
    // incrementally on edits.
    uint8_t sunLight(int x, int y, int z) const {
        if (y >= CHUNK_HEIGHT) return 15;
        if (y < 0) return 0;
        return light_[index(x, y, z)] & 0x0F;
    }
    uint8_t blockLight(int x, int y, int z) const {
        if (y < 0 || y >= CHUNK_HEIGHT) return 0;
        return light_[index(x, y, z)] >> 4;
    }
    void setSunLight(int x, int y, int z, uint8_t v) {
        if (y < 0 || y >= CHUNK_HEIGHT) return;
        uint8_t& l = light_[index(x, y, z)];
        l = (l & 0xF0) | v;
    }
    void setBlockLight(int x, int y, int z, uint8_t v) {
        if (y < 0 || y >= CHUNK_HEIGHT) return;
        uint8_t& l = light_[index(x, y, z)];
        l = (l & 0x0F) | uint8_t(v << 4);
    }
    uint8_t packedLight(int x, int y, int z) const {
        if (y >= CHUNK_HEIGHT) return 0x0F; // open sky
        if (y < 0) return 0;
        return light_[index(x, y, z)];
    }

    // Sunlight column fill + in-chunk BFS for both channels. Pure CPU work on
    // this chunk only, so generation workers may call it on fresh chunks;
    // cross-border propagation happens later on the main thread.
    void computeInitialLight();

    // Lifecycle flags (all owned by the main thread).
    bool dirty = true;          // blocks changed since last mesh enqueue
    bool queuedDirty = false;   // already registered in World's dirty queue
    bool meshInFlight = false;  // a mesh job for this chunk is in the pipeline
    bool modified = false;      // player changed blocks -> needs saving

    void uploadMesh(const MeshData& md); // GL thread only
    void draw() const;
    void drawWater() const; // translucent pass, after all opaque chunks
    bool hasMesh() const { return vao_ != 0; }
    // Anything to draw in each pass? Lets the draw loops skip empty chunks
    // (most chunks have no water) without binding/setting uniforms.
    bool hasOpaque() const { return vao_ != 0 && indexCount_ > 0; }
    bool hasWater() const { return waterVao_ != 0 && waterIndexCount_ > 0; }

    // Persistence: raw block dump.
    const uint8_t* rawData() const { return reinterpret_cast<const uint8_t*>(blocks_.data()); }
    uint8_t* rawData() { return reinterpret_cast<uint8_t*>(blocks_.data()); }
    static size_t rawSize() { return size_t(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE; }

private:
    static int index(int x, int y, int z) {
        return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x;
    }

    int cx_, cz_;
    std::vector<Block> blocks_;
    std::vector<uint8_t> light_;

    unsigned vao_ = 0, vbo_ = 0, ebo_ = 0;
    int indexCount_ = 0;
    unsigned waterVao_ = 0, waterVbo_ = 0, waterEbo_ = 0;
    int waterIndexCount_ = 0;
};
