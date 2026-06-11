#include "Chunk.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <algorithm>
#include <cmath>

Chunk::Chunk(int cx, int cz)
    : cx_(cx), cz_(cz),
      blocks_(size_t(CHUNK_SIZE) * CHUNK_HEIGHT * CHUNK_SIZE, Block::Air),
      light_(blocks_.size(), 0) {}

Chunk::~Chunk() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (waterVao_) glDeleteVertexArrays(1, &waterVao_);
    if (waterVbo_) glDeleteBuffers(1, &waterVbo_);
    if (waterEbo_) glDeleteBuffers(1, &waterEbo_);
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
// UV corners matching the winding above (u right, v up)
const float FACE_UV[4][2] = {{0,0},{1,0},{1,1},{0,1}};

// Light level -> brightness, roughly 0.85^(15-level) so each level lost dims
// by 15%; floored so unlit caves stay barely navigable instead of pure black.
const float LIGHT_CURVE[16] = {
    0.090f, 0.103f, 0.121f, 0.142f, 0.167f, 0.197f, 0.232f, 0.272f,
    0.321f, 0.377f, 0.444f, 0.522f, 0.614f, 0.722f, 0.850f, 1.000f,
};
// Ambient occlusion level (0 = fully occluded corner .. 3 = open) ->
// brightness multiplier.
const float AO_CURVE[4] = {0.55f, 0.72f, 0.86f, 1.0f};
}

void Chunk::computeInitialLight() {
    constexpr int CS = CHUNK_SIZE;
    std::fill(light_.begin(), light_.end(), 0);

    // Worklists of flat indices; head pointer instead of pop_front.
    std::vector<int> sunQ, blkQ;
    sunQ.reserve(8192);

    // Sunlight: each column is 15 from the sky down to the first opaque or
    // dimming (water) block; water then gets attenuated light via the BFS.
    for (int z = 0; z < CS; ++z) {
        for (int x = 0; x < CS; ++x) {
            for (int y = CHUNK_HEIGHT - 1; y >= 0; --y) {
                int i = index(x, y, z);
                if (isOpaque(blocks_[i]) || dimsSunlight(blocks_[i])) break;
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
                // Full sunlight keeps level 15 straight down (no attenuation),
                // unless it enters water, which dims it like a sideways step.
                uint8_t target = (sun && d[1] == -1 && l == 15 &&
                                  !dimsSunlight(blocks_[ni])) ? 15 : l - 1;
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

namespace {
// Greedy meshing scans each face direction as a stack of 2D slices. For each
// face: the slice axis (na, with sign ns), and the slice-grid axes ua/va with
// the direction (su/sv) the face's texture u/v runs along them — derived from
// the FACES corner winding so merged quads keep the exact orientation the
// per-cell faces had.
struct FaceAxes { int na, ns, ua, su, va, sv; };
const FaceAxes AXES[6] = {
    {0, +1, 2, -1, 1, +1}, // +X: u along -Z, v along +Y
    {0, -1, 2, +1, 1, +1}, // -X
    {1, +1, 0, +1, 2, -1}, // +Y: u along +X, v along -Z
    {1, -1, 0, +1, 2, +1}, // -Y
    {2, +1, 0, +1, 1, +1}, // +Z
    {2, -1, 0, -1, 1, +1}, // -Z
};
const int AXIS_SIZE[3] = {CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE};
}

MeshData buildMeshData(const ChunkSnapshot& s) {
    constexpr int CS = CHUNK_SIZE;
    // AO/smooth-light corner samples reach one cell diagonally out of the
    // chunk, so both x and z can be out of range at once (corner columns).
    auto blockAt = [&](int x, int y, int z) -> Block {
        if (y < 0 || y >= CHUNK_HEIGHT) return Block::Air;
        bool xn = x < 0, xp = x >= CS, zn = z < 0, zp = z >= CS;
        if ((xn || xp) && (zn || zp)) {
            const std::vector<Block>& c = xn ? (zn ? s.cornerXnZn : s.cornerXnZp)
                                             : (zn ? s.cornerXpZn : s.cornerXpZp);
            return c.empty() ? Block::Air : c[y];
        }
        if (xn) return s.edgeXn.empty() ? Block::Air : s.edgeXn[y * CS + z];
        if (xp) return s.edgeXp.empty() ? Block::Air : s.edgeXp[y * CS + z];
        if (zn) return s.edgeZn.empty() ? Block::Air : s.edgeZn[y * CS + x];
        if (zp) return s.edgeZp.empty() ? Block::Air : s.edgeZp[y * CS + x];
        return s.blocks[(y * CS + z) * CS + x];
    };
    // Packed light (sun low nibble, block high nibble) of a cell. Missing
    // data (above the world, absent neighbor, bare test snapshot) counts as
    // fully sunlit with no block light.
    auto lightAt = [&](int x, int y, int z) -> uint8_t {
        if (y >= CHUNK_HEIGHT) return 0x0F;
        if (y < 0) return 0;
        bool xn = x < 0, xp = x >= CS, zn = z < 0, zp = z >= CS;
        if ((xn || xp) && (zn || zp)) {
            const std::vector<uint8_t>& c =
                xn ? (zn ? s.cornerLightXnZn : s.cornerLightXnZp)
                   : (zn ? s.cornerLightXpZn : s.cornerLightXpZp);
            return c.empty() ? 0x0F : c[y];
        }
        if (xn) return s.lightXn.empty() ? 0x0F : s.lightXn[y * CS + z];
        if (xp) return s.lightXp.empty() ? 0x0F : s.lightXp[y * CS + z];
        if (zn) return s.lightZn.empty() ? 0x0F : s.lightZn[y * CS + x];
        if (zp) return s.lightZp.empty() ? 0x0F : s.lightZp[y * CS + x];
        return s.light.empty() ? 0x0F : s.light[(y * CS + z) * CS + x];
    };

    // Per-vertex brightness of one face cell: directional face shade x smooth
    // light x ambient occlusion, quantized to a byte — separately for the sun
    // and block-light channels (qs/qb), so the shader can dim sunlight with
    // the day/night cycle while torches keep glowing. Each corner samples the
    // four cells around it in the plane the face looks into; water keeps flat
    // per-face light (a lake stays one even sheet). The quantized tuples are
    // also the greedy merge key, so equal tuples shade identically.
    auto shadeFace = [&](int f, int nx, int ny, int nz, bool water,
                         uint8_t qs[4], uint8_t qb[4]) {
        if (water) {
            uint8_t p = lightAt(nx, ny, nz);
            float fs = FACES[f].light * LIGHT_CURVE[p & 0x0F];
            float fb = FACES[f].light * LIGHT_CURVE[p >> 4];
            qs[0] = qs[1] = qs[2] = qs[3] = uint8_t(std::lround(fs * 255.0f));
            qb[0] = qb[1] = qb[2] = qb[3] = uint8_t(std::lround(fb * 255.0f));
            return;
        }
        const FaceDef& fd = FACES[f];
        int axis = f >> 1; // 0 X, 1 Y, 2 Z
        int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
        for (int v = 0; v < 4; ++v) {
            int s1 = fd.corners[v][a1] > 0.5f ? 1 : -1;
            int s2 = fd.corners[v][a2] > 0.5f ? 1 : -1;
            int c1[3] = {nx, ny, nz}; c1[a1] += s1;
            int c2[3] = {nx, ny, nz}; c2[a2] += s2;
            int cc[3] = {nx, ny, nz}; cc[a1] += s1; cc[a2] += s2;
            bool o1 = isOpaque(blockAt(c1[0], c1[1], c1[2]));
            bool o2 = isOpaque(blockAt(c2[0], c2[1], c2[2]));
            bool oc = isOpaque(blockAt(cc[0], cc[1], cc[2]));
            // Classic 3-neighbor AO: both sides solid fully occludes the
            // corner regardless of the diagonal.
            int ao = (o1 && o2) ? 0 : 3 - (int(o1) + int(o2) + int(oc));
            // Smooth light: average the open cells around the corner. The
            // diagonal is skipped when both sides block it, so light can't
            // leak around an edge.
            uint8_t p = lightAt(nx, ny, nz);
            float sunSum = LIGHT_CURVE[p & 0x0F], blkSum = LIGHT_CURVE[p >> 4];
            int cnt = 1;
            auto add = [&](int x, int y, int z) {
                uint8_t q = lightAt(x, y, z);
                sunSum += LIGHT_CURVE[q & 0x0F];
                blkSum += LIGHT_CURVE[q >> 4];
                ++cnt;
            };
            if (!o1) add(c1[0], c1[1], c1[2]);
            if (!o2) add(c2[0], c2[1], c2[2]);
            if (!oc && !(o1 && o2)) add(cc[0], cc[1], cc[2]);
            float base = FACES[f].light * AO_CURVE[ao] / cnt;
            qs[v] = uint8_t(std::lround(base * sunSum * 255.0f));
            qb[v] = uint8_t(std::lround(base * blkSum * 255.0f));
        }
    };

    MeshData md;
    md.verts.reserve(4096);
    md.inds.reserve(2048);

    // A w x h greedy quad. Corner v carries FACE_UV[v] scaled by the quad
    // size; su/sv map that uv back to slice-grid coordinates, which keeps
    // texture orientation identical to the old per-cell faces (REPEAT
    // wrapping makes the integer offset between cells invisible).
    auto emitQuad = [](std::vector<ChunkVertex>& verts, std::vector<uint32_t>& inds,
                       int f, int n, int gu, int gv, int w, int h,
                       int tile, const uint8_t qs[4], const uint8_t qb[4]) {
        const FaceAxes& A = AXES[f];
        int plane = n + (A.ns > 0 ? 1 : 0);
        uint32_t base = uint32_t(verts.size());
        for (int v = 0; v < 4; ++v) {
            int fu = int(FACE_UV[v][0]), fv = int(FACE_UV[v][1]);
            int pos[3];
            pos[A.na] = plane;
            pos[A.ua] = A.su > 0 ? gu + fu * w : gu + w - fu * w;
            pos[A.va] = A.sv > 0 ? gv + fv * h : gv + h - fv * h;
            verts.push_back({uint16_t(pos[0] * 16), uint16_t(pos[1] * 16),
                             uint16_t(pos[2] * 16),
                             uint16_t(fu * w * 16), uint16_t(fv * h * 16),
                             qs[v], qb[v], uint8_t(tile), 0});
        }
        // Split the quad along the diagonal with the smaller brightness sum,
        // so AO corners shade as corners instead of bleeding across the whole
        // face (anisotropy fix). Daytime brightness (sun dominates) decides.
        uint8_t q[4];
        for (int v = 0; v < 4; ++v) q[v] = std::max(qs[v], qb[v]);
        if (int(q[0]) + q[2] > int(q[1]) + q[3]) {
            inds.push_back(base + 0); inds.push_back(base + 1); inds.push_back(base + 3);
            inds.push_back(base + 1); inds.push_back(base + 2); inds.push_back(base + 3);
        } else {
            inds.push_back(base + 0); inds.push_back(base + 1); inds.push_back(base + 2);
            inds.push_back(base + 0); inds.push_back(base + 2); inds.push_back(base + 3);
        }
    };

    // Everything above the highest non-air cell is open sky: no faces up
    // there, so all six sweeps clamp their y range to it.
    int topY = CHUNK_HEIGHT - 1;
    for (; topY >= 0; --topY) {
        const Block* layer = &s.blocks[size_t(topY) * CS * CS];
        bool any = false;
        for (int i = 0; i < CS * CS; ++i)
            if (layer[i] != Block::Air) { any = true; break; }
        if (any) break;
    }
    if (topY < 0) return md;

    // Greedy pass: per face direction, per slice, build a mask of visible
    // face cells keyed by (block, corner-brightness tuple), then merge
    // maximal rectangles of equal keys. Equal keys mean equal tile and equal
    // shading at every cell, so the merged quad renders the same.
    const int STRIDE[3] = {1, CS * CS, CS}; // flat-index step per axis
    // Merge key: block id + the 4-corner tuple of BOTH light channels
    // (first carries the visible flag, block, and sun bytes; second the
    // block-light bytes) — 12 shading bytes no longer fit one uint64.
    using Key = std::pair<uint64_t, uint64_t>;
    for (int f = 0; f < 6; ++f) {
        const FaceAxes& A = AXES[f];
        const int NU = AXIS_SIZE[A.ua], NN = AXIS_SIZE[A.na];
        const int NV = A.va == 1 ? topY + 1 : AXIS_SIZE[A.va];
        const int NNlim = A.na == 1 ? topY + 1 : NN;
        std::vector<Key> mask(size_t(NU) * NV);
        for (int n = 0; n < NNlim; ++n) {
            // The face's neighbor cell leaves the chunk only on the outermost
            // slice; everywhere else it is a direct strided index away.
            const bool edgeSlice = A.ns > 0 ? n == NN - 1 : n == 0;
            const int nstep = A.ns * STRIDE[A.na];
            bool any = false;
            for (int gv = 0; gv < NV; ++gv) {
                int p[3]; p[A.na] = n; p[A.ua] = 0; p[A.va] = gv;
                int idx = (p[1] * CS + p[2]) * CS + p[0];
                for (int gu = 0; gu < NU; ++gu, idx += STRIDE[A.ua]) {
                    Key key{0, 0};
                    Block b = s.blocks[idx];
                    if (b != Block::Air && b != Block::Torch) {
                        p[A.ua] = gu;
                        int np[3] = {p[0], p[1], p[2]};
                        np[A.na] += A.ns;
                        Block nb = edgeSlice ? blockAt(np[0], np[1], np[2])
                                             : s.blocks[idx + nstep];
                        bool water = (b == Block::Water);
                        // Water faces show only against air/torch: water-water
                        // and water-opaque pairs are culled, so a lake is one
                        // surface. Non-cubes (torch, water) don't hide faces.
                        bool visible = water ? !(nb == Block::Water || isOpaque(nb))
                                             : !isOpaque(nb);
                        if (visible) {
                            uint8_t qs[4], qb[4];
                            shadeFace(f, np[0], np[1], np[2], water, qs, qb);
                            key.first = (1ull << 40) | (uint64_t(b) << 32) |
                                        (uint64_t(qs[0]) << 24) | (uint64_t(qs[1]) << 16) |
                                        (uint64_t(qs[2]) << 8) | qs[3];
                            key.second = (uint64_t(qb[0]) << 24) | (uint64_t(qb[1]) << 16) |
                                         (uint64_t(qb[2]) << 8) | qb[3];
                            any = true;
                        }
                    }
                    mask[size_t(gv) * NU + gu] = key;
                }
            }
            if (!any) continue;
            for (int gv = 0; gv < NV; ++gv) {
                for (int gu = 0; gu < NU;) {
                    Key key = mask[size_t(gv) * NU + gu];
                    if (!key.first) { ++gu; continue; }
                    int w = 1;
                    while (gu + w < NU && mask[size_t(gv) * NU + gu + w] == key) ++w;
                    int h = 1;
                    for (; gv + h < NV; ++h) {
                        bool rowOk = true;
                        for (int i = 0; i < w; ++i)
                            if (mask[size_t(gv + h) * NU + gu + i] != key) { rowOk = false; break; }
                        if (!rowOk) break;
                    }
                    for (int j = 0; j < h; ++j)
                        for (int i = 0; i < w; ++i)
                            mask[size_t(gv + j) * NU + gu + i] = {0, 0};
                    Block b = Block((key.first >> 32) & 0xFF);
                    uint8_t qs[4] = {uint8_t(key.first >> 24), uint8_t(key.first >> 16),
                                     uint8_t(key.first >> 8), uint8_t(key.first)};
                    uint8_t qb[4] = {uint8_t(key.second >> 24), uint8_t(key.second >> 16),
                                     uint8_t(key.second >> 8), uint8_t(key.second)};
                    bool water = (b == Block::Water);
                    emitQuad(water ? md.waterVerts : md.verts,
                             water ? md.waterInds : md.inds,
                             f, n, gu, gv, w, h, tileFor(b, f), qs, qb);
                    gu += w;
                }
            }
        }
    }

    // Torches are custom thin-post geometry (2/16 wide, 10/16 tall), never
    // merged and never culled by neighbors. All its measurements are integer
    // 1/16ths, so the packed format holds them exactly.
    for (int y = 0; y <= topY; ++y) {
        for (int z = 0; z < CS; ++z) {
            for (int x = 0; x < CS; ++x) {
                if (s.blocks[(y * CS + z) * CS + x] != Block::Torch) continue;
                // Lit by its own cell — the block channel at least its own
                // emission, so the flame glows through the night; no
                // directional shading so it glows evenly.
                uint8_t p = lightAt(x, y, z);
                int bl = p >> 4;
                if (int(lightEmission(Block::Torch)) > bl)
                    bl = lightEmission(Block::Torch);
                uint8_t qsun = uint8_t(std::lround(LIGHT_CURVE[p & 0x0F] * 255.0f));
                uint8_t qblk = uint8_t(std::lround(LIGHT_CURVE[bl] * 255.0f));
                for (int f = 0; f < 6; ++f) {
                    // The bottom face is usually flush with the ground.
                    if (f == 3 && isOpaque(blockAt(x, y - 1, z))) continue;
                    const FaceDef& fd = FACES[f];
                    uint32_t base = uint32_t(md.verts.size());
                    for (int v = 0; v < 4; ++v) {
                        int fu = int(FACE_UV[v][0]), fv = int(FACE_UV[v][1]);
                        // Sides sample the tile's central 2px strip (stick +
                        // flame); top/bottom sample 2x2 flame/wood caps.
                        int vv = f == 2 ? 8 + 2 * fv : f == 3 ? 2 * fv : 10 * fv;
                        md.verts.push_back(
                            {uint16_t(x * 16 + 7 + int(fd.corners[v][0]) * 2),
                             uint16_t(y * 16 + int(fd.corners[v][1]) * 10),
                             uint16_t(z * 16 + 7 + int(fd.corners[v][2]) * 2),
                             uint16_t(7 + 2 * fu), uint16_t(vv),
                             qsun, qblk, uint8_t(tileFor(Block::Torch, f)), 0});
                    }
                    md.inds.push_back(base + 0); md.inds.push_back(base + 1); md.inds.push_back(base + 2);
                    md.inds.push_back(base + 0); md.inds.push_back(base + 2); md.inds.push_back(base + 3);
                }
            }
        }
    }
    return md;
}

namespace {
void uploadOne(unsigned& vao, unsigned& vbo, unsigned& ebo,
               const std::vector<ChunkVertex>& verts, const std::vector<uint32_t>& inds) {
    if (!vao) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
    }
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(ChunkVertex), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, inds.size() * sizeof(uint32_t), inds.data(), GL_STATIC_DRAW);

    // Integer attributes (the shader unpacks): position, uv, sun+blk+layer.
    const GLsizei stride = sizeof(ChunkVertex);
    glVertexAttribIPointer(0, 3, GL_UNSIGNED_SHORT, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, stride, (void*)6);
    glEnableVertexAttribArray(1);
    glVertexAttribIPointer(2, 3, GL_UNSIGNED_BYTE, stride, (void*)10);
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}
}

void Chunk::uploadMesh(const MeshData& md) {
    uploadOne(vao_, vbo_, ebo_, md.verts, md.inds);
    indexCount_ = (int)md.inds.size();
    // Most chunks have no water: skip allocating empty GL buffers for them.
    if (!md.waterInds.empty() || waterVao_) {
        uploadOne(waterVao_, waterVbo_, waterEbo_, md.waterVerts, md.waterInds);
    }
    waterIndexCount_ = (int)md.waterInds.size();
}

void Chunk::draw() const {
    if (!vao_ || indexCount_ == 0) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
}

void Chunk::drawWater() const {
    if (!waterVao_ || waterIndexCount_ == 0) return;
    glBindVertexArray(waterVao_);
    glDrawElements(GL_TRIANGLES, waterIndexCount_, GL_UNSIGNED_INT, nullptr);
}
