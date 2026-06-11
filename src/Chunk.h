#pragma once
#include "Block.h"
#include <cstdint>
#include <vector>

constexpr int CHUNK_SIZE = 16;   // X and Z
constexpr int CHUNK_HEIGHT = 80; // Y

// CPU-side mesh, built on any thread, uploaded on the GL thread.
struct MeshData {
    std::vector<float> verts;   // x y z u v light
    std::vector<uint32_t> inds;
};

// Self-contained copy of everything mesh building needs: the chunk's blocks
// plus one-block-deep slices of the four side neighbors. Workers read only
// this, never live chunks.
struct ChunkSnapshot {
    int cx = 0, cz = 0;
    std::vector<Block> blocks;                       // [(y*CS+z)*CS+x]
    std::vector<Block> edgeXn, edgeXp, edgeZn, edgeZp; // [y*CS + (z or x)]
};

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

    // Lifecycle flags (all owned by the main thread).
    bool dirty = true;          // blocks changed since last mesh enqueue
    bool meshInFlight = false;  // a mesh job for this chunk is in the pipeline
    bool modified = false;      // player changed blocks -> needs saving

    void uploadMesh(const MeshData& md); // GL thread only
    void draw() const;
    bool hasMesh() const { return vao_ != 0; }

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

    unsigned vao_ = 0, vbo_ = 0, ebo_ = 0;
    int indexCount_ = 0;
};
