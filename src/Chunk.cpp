#include "Chunk.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <algorithm>

Chunk::Chunk(int cx, int cz)
    : cx_(cx), cz_(cz),
      blocks_(size_t(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE, Block::Air),
      light_(blocks_.size(), 0) {}

Chunk::~Chunk() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
}

namespace {
// Per-face data: 4 corner offsets, normal-axis shading
struct FaceDef {
    float corners[4][3];
    float light;
};
// Face order: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z
const FaceDef FACES[6] = {
    {{{1,0,1},{1,0,0},{1,1,0},{1,1,1}}, 0.6f}, // +X
    {{{0,0,0},{0,0,1},{0,1,1},{0,1,0}}, 0.6f}, // -X
    {{{0,1,1},{1,1,1},{1,1,0},{0,1,0}}, 1.0f}, // +Y
    {{{0,0,0},{1,0,0},{1,0,1},{0,0,1}}, 0.5f}, // -Y
    {{{0,0,1},{1,0,1},{1,1,1},{0,1,1}}, 0.8f}, // +Z
    {{{1,0,0},{0,0,0},{0,1,0},{1,1,0}}, 0.8f}, // -Z
};
const int FACE_DIR[6][3] = {
    {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
};
// UV corners matching the winding above (u right, v up)
const float FACE_UV[4][2] = {{0,0},{1,0},{1,1},{0,1}};

// Light level -> brightness, roughly 0.85^(15-level) so each level lost dims
// by 15%; floored so unlit caves stay barely navigable instead of pure black.
const float LIGHT_CURVE[16] = {
    0.090f, 0.103f, 0.121f, 0.142f, 0.167f, 0.197f, 0.232f, 0.272f,
    0.321f, 0.377f, 0.444f, 0.522f, 0.614f, 0.722f, 0.850f, 1.000f,
};
}

void Chunk::computeInitialLight() {
    constexpr int CS = CHUNK_SIZE;
    std::fill(light_.begin(), light_.end(), 0);

    // Worklists of flat indices; head pointer instead of pop_front.
    std::vector<int> sunQ, blkQ;
    sunQ.reserve(8192);

    // Sunlight: each column is 15 from the sky down to the first opaque block.
    for (int z = 0; z < CS; ++z) {
        for (int x = 0; x < CS; ++x) {
            for (int y = CHUNK_HEIGHT - 1; y >= 0; --y) {
                int i = index(x, y, z);
                if (isOpaque(blocks_[i])) break;
                light_[i] = 15;
                sunQ.push_back(i);
            }
        }
    }
    // Block light sources (loaded chunks may contain torches).
    for (size_t i = 0; i < blocks_.size(); ++i) {
        uint8_t e = lightEmission(blocks_[i]);
        if (e) { light_[i] |= uint8_t(e << 4); blkQ.push_back((int)i); }
    }

    static const int D[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    auto spread = [&](std::vector<int>& q, bool sun) {
        for (size_t h = 0; h < q.size(); ++h) {
            int i = q[h];
            int x = i % CS, z = (i / CS) % CS, y = i / (CS * CS);
            uint8_t l = sun ? (light_[i] & 0x0F) : (light_[i] >> 4);
            if (l <= 1) continue;
            for (auto& d : D) {
                int nx = x + d[0], ny = y + d[1], nz = z + d[2];
                if (nx < 0 || nx >= CS || nz < 0 || nz >= CS ||
                    ny < 0 || ny >= CHUNK_HEIGHT) continue;
                int ni = index(nx, ny, nz);
                if (isOpaque(blocks_[ni])) continue;
                // Full sunlight keeps level 15 straight down (no attenuation).
                uint8_t target = (sun && d[1] == -1 && l == 15) ? 15 : l - 1;
                uint8_t nl = sun ? (light_[ni] & 0x0F) : (light_[ni] >> 4);
                if (nl < target) {
                    if (sun) light_[ni] = (light_[ni] & 0xF0) | target;
                    else     light_[ni] = (light_[ni] & 0x0F) | uint8_t(target << 4);
                    q.push_back(ni);
                }
            }
        }
    };
    spread(sunQ, true);
    spread(blkQ, false);
}

MeshData buildMeshData(const ChunkSnapshot& s) {
    constexpr int CS = CHUNK_SIZE;
    auto blockAt = [&](int x, int y, int z) -> Block {
        if (y < 0 || y >= CHUNK_HEIGHT) return Block::Air;
        // Only one coordinate can be out of range (face neighbors are axis-aligned).
        if (x < 0)   return s.edgeXn.empty() ? Block::Air : s.edgeXn[y * CS + z];
        if (x >= CS) return s.edgeXp.empty() ? Block::Air : s.edgeXp[y * CS + z];
        if (z < 0)   return s.edgeZn.empty() ? Block::Air : s.edgeZn[y * CS + x];
        if (z >= CS) return s.edgeZp.empty() ? Block::Air : s.edgeZp[y * CS + x];
        return s.blocks[(y * CS + z) * CS + x];
    };
    // Combined light level (max of sun and block light) of the cell a face
    // looks into. Missing data (above the world, absent neighbor, bare test
    // snapshot) counts as fully sunlit.
    auto lightAt = [&](int x, int y, int z) -> int {
        auto lvl = [](uint8_t p) { int sl = p & 0x0F, bl = p >> 4; return sl > bl ? sl : bl; };
        if (y >= CHUNK_HEIGHT) return 15;
        if (y < 0) return 0;
        if (x < 0)   return s.lightXn.empty() ? 15 : lvl(s.lightXn[y * CS + z]);
        if (x >= CS) return s.lightXp.empty() ? 15 : lvl(s.lightXp[y * CS + z]);
        if (z < 0)   return s.lightZn.empty() ? 15 : lvl(s.lightZn[y * CS + x]);
        if (z >= CS) return s.lightZp.empty() ? 15 : lvl(s.lightZp[y * CS + x]);
        return s.light.empty() ? 15 : lvl(s.light[(y * CS + z) * CS + x]);
    };

    MeshData md;
    md.verts.reserve(8192);
    md.inds.reserve(4096);

    const int bx = s.cx * CS;
    const int bz = s.cz * CS;
    const float tileW = 1.0f / float(ATLAS_TILES);

    for (int y = 0; y < CHUNK_HEIGHT; ++y) {
        for (int z = 0; z < CS; ++z) {
            for (int x = 0; x < CS; ++x) {
                Block b = s.blocks[(y * CS + z) * CS + x];
                if (b == Block::Air) continue;

                if (b == Block::Torch) {
                    // A torch is a thin post (2/16 wide, 10/16 tall), not a
                    // cube: emit its own little box, never culled by
                    // neighbors. Lit by its own cell (at least its emission).
                    int level = lightAt(x, y, z);
                    if (int(lightEmission(b)) > level) level = lightEmission(b);
                    const float lo = 7.0f / 16.0f, w = 2.0f / 16.0f, top = 10.0f / 16.0f;
                    for (int f = 0; f < 6; ++f) {
                        // The bottom face is usually flush with the ground.
                        if (f == 3 && isOpaque(blockAt(x, y - 1, z))) continue;
                        const FaceDef& fd = FACES[f];
                        // No directional shading: the flame should glow evenly.
                        float light = LIGHT_CURVE[level];
                        uint32_t base = uint32_t(md.verts.size() / 6);
                        for (int v = 0; v < 4; ++v) {
                            md.verts.push_back(float(bx + x) + lo + fd.corners[v][0] * w);
                            md.verts.push_back(float(y)      + fd.corners[v][1] * top);
                            md.verts.push_back(float(bz + z) + lo + fd.corners[v][2] * w);
                            // Sides sample the tile's central 2px strip (stick
                            // + flame); top/bottom sample 2x2 flame/wood caps.
                            float fu = FACE_UV[v][0], fv = FACE_UV[v][1];
                            float u = (7.0f + 2.0f * fu) / 16.0f;
                            float vv = f == 2 ? (8.0f + 2.0f * fv) / 16.0f
                                     : f == 3 ? (2.0f * fv) / 16.0f
                                              : (10.0f * fv) / 16.0f;
                            md.verts.push_back((float(tileFor(b, f)) + u) * tileW);
                            md.verts.push_back(vv);
                            md.verts.push_back(light);
                        }
                        md.inds.push_back(base + 0); md.inds.push_back(base + 1); md.inds.push_back(base + 2);
                        md.inds.push_back(base + 0); md.inds.push_back(base + 2); md.inds.push_back(base + 3);
                    }
                    continue;
                }

                for (int f = 0; f < 6; ++f) {
                    int nx = x + FACE_DIR[f][0], ny = y + FACE_DIR[f][1], nz = z + FACE_DIR[f][2];
                    Block nb = blockAt(nx, ny, nz);
                    if (isOpaque(nb)) continue; // non-cubes (torches) don't hide faces

                    int tile = tileFor(b, f);
                    float u0 = tile * tileW;
                    // The face is lit by the cell it looks into.
                    float light = FACES[f].light * LIGHT_CURVE[lightAt(nx, ny, nz)];
                    uint32_t base = uint32_t(md.verts.size() / 6);
                    const FaceDef& fd = FACES[f];
                    for (int v = 0; v < 4; ++v) {
                        md.verts.push_back(float(bx + x) + fd.corners[v][0]);
                        md.verts.push_back(float(y)      + fd.corners[v][1]);
                        md.verts.push_back(float(bz + z) + fd.corners[v][2]);
                        md.verts.push_back(u0 + FACE_UV[v][0] * tileW);
                        md.verts.push_back(FACE_UV[v][1]);
                        md.verts.push_back(light);
                    }
                    md.inds.push_back(base + 0); md.inds.push_back(base + 1); md.inds.push_back(base + 2);
                    md.inds.push_back(base + 0); md.inds.push_back(base + 2); md.inds.push_back(base + 3);
                }
            }
        }
    }
    return md;
}

void Chunk::uploadMesh(const MeshData& md) {
    if (!vao_) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);
    }
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, md.verts.size() * sizeof(float), md.verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, md.inds.size() * sizeof(uint32_t), md.inds.data(), GL_STATIC_DRAW);

    const GLsizei stride = 6 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    indexCount_ = (int)md.inds.size();
}

void Chunk::draw() const {
    if (!vao_ || indexCount_ == 0) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
}
