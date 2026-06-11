#include "Chunk.h"
#include <GL/gl.h>
#include <GL/glext.h>

Chunk::Chunk(int cx, int cz)
    : cx_(cx), cz_(cz), blocks_(size_t(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE, Block::Air) {}

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
                for (int f = 0; f < 6; ++f) {
                    Block nb = blockAt(x + FACE_DIR[f][0], y + FACE_DIR[f][1], z + FACE_DIR[f][2]);
                    if (isSolid(nb)) continue;

                    int tile = tileFor(b, f);
                    float u0 = tile * tileW;
                    uint32_t base = uint32_t(md.verts.size() / 6);
                    const FaceDef& fd = FACES[f];
                    for (int v = 0; v < 4; ++v) {
                        md.verts.push_back(float(bx + x) + fd.corners[v][0]);
                        md.verts.push_back(float(y)      + fd.corners[v][1]);
                        md.verts.push_back(float(bz + z) + fd.corners[v][2]);
                        md.verts.push_back(u0 + FACE_UV[v][0] * tileW);
                        md.verts.push_back(FACE_UV[v][1]);
                        md.verts.push_back(fd.light);
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
