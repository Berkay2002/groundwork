#include "Lighting.h"
#include <utility>
#include <vector>

namespace lighting {
namespace {
const glm::ivec3 LIGHT_DIRS[6] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

void setLight(Accessor& a, Channel ch, int wx, int wy, int wz, uint8_t v) {
    if (wy < 0 || wy >= CHUNK_HEIGHT) return;
    if (!a.hasChunkAt(wx, wz)) return;
    if (a.cellLightAt(ch, wx, wy, wz) == v) return;
    a.setCellLight(ch, wx, wy, wz, v);
}

void addLight(Accessor& a, Channel ch, std::vector<glm::ivec3> q) {
    for (size_t h = 0; h < q.size(); ++h) {
        glm::ivec3 p = q[h];
        uint8_t l = lightAt(a, ch, p.x, p.y, p.z);
        if (l <= 1) continue;
        for (const glm::ivec3& d : LIGHT_DIRS) {
            glm::ivec3 n = p + d;
            if (n.y < 0 || n.y >= CHUNK_HEIGHT) continue;
            if (!a.hasChunkAt(n.x, n.z)) continue;
            Block nb = a.blockAt(n.x, n.y, n.z);
            if (isOpaque(nb)) continue;
            uint8_t target = (ch == Channel::Sun && d.y == -1 && l == 15 &&
                              !dimsSunlight(nb)) ? 15 : l - 1;
            if (lightAt(a, ch, n.x, n.y, n.z) < target) {
                setLight(a, ch, n.x, n.y, n.z, target);
                q.push_back(n);
            }
        }
    }
}

void removeLight(Accessor& a, Channel ch, const glm::ivec3& pos) {
    uint8_t old = lightAt(a, ch, pos.x, pos.y, pos.z);
    if (old == 0) return;
    setLight(a, ch, pos.x, pos.y, pos.z, 0);
    std::vector<std::pair<glm::ivec3, uint8_t>> q{{pos, old}};
    std::vector<glm::ivec3> readd;
    for (size_t h = 0; h < q.size(); ++h) {
        glm::ivec3 p = q[h].first;
        uint8_t l = q[h].second;
        for (const glm::ivec3& d : LIGHT_DIRS) {
            glm::ivec3 n = p + d;
            if (n.y < 0 || n.y >= CHUNK_HEIGHT) continue;
            uint8_t nl = lightAt(a, ch, n.x, n.y, n.z);
            if (nl == 0) continue;
            // Full sunlight propagates down lossless, so a 15 below a
            // removed 15 depends on it despite not being dimmer.
            if (nl < l || (ch == Channel::Sun && d.y == -1 && l == 15)) {
                setLight(a, ch, n.x, n.y, n.z, 0);
                q.push_back({n, nl});
            } else {
                readd.push_back(n);
            }
        }
    }
    addLight(a, ch, std::move(readd));
}
}

uint8_t lightAt(const ReadAccess& a, Channel ch, int wx, int wy, int wz) {
    if (wy >= CHUNK_HEIGHT) return ch == Channel::Sun ? 15 : 0;
    if (wy < 0) return 0;
    if (!a.hasChunkAt(wx, wz)) return 0;
    return a.cellLightAt(ch, wx, wy, wz);
}

void onBlockChanged(Accessor& a, Block oldBlock, Block newBlock, const glm::ivec3& pos) {
    std::vector<glm::ivec3> around = {
        {pos.x + 1, pos.y, pos.z}, {pos.x - 1, pos.y, pos.z},
        {pos.x, pos.y + 1, pos.z}, {pos.x, pos.y - 1, pos.z},
        {pos.x, pos.y, pos.z + 1}, {pos.x, pos.y, pos.z - 1},
    };

    if (lightEmission(oldBlock) > 0 || (isOpaque(newBlock) && !isOpaque(oldBlock)))
        removeLight(a, Channel::Block, pos);
    if (lightEmission(newBlock) > 0) {
        setLight(a, Channel::Block, pos.x, pos.y, pos.z, lightEmission(newBlock));
        addLight(a, Channel::Block, {pos});
    } else if (!isOpaque(newBlock)) {
        addLight(a, Channel::Block, around);
    }

    bool blocksSunNow = isOpaque(newBlock) || dimsSunlight(newBlock);
    bool blockedSunBefore = isOpaque(oldBlock) || dimsSunlight(oldBlock);
    if (blocksSunNow && !blockedSunBefore)
        removeLight(a, Channel::Sun, pos);
    if (!isOpaque(newBlock))
        addLight(a, Channel::Sun, around);
}

void onChunkAdded(Accessor& a, int cx, int cz) {
    std::vector<glm::ivec3> sunSeeds, blockSeeds;
    int bx = cx * CHUNK_SIZE, bz = cz * CHUNK_SIZE;
    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& d : dirs) {
        int ncx = cx + d[0], ncz = cz + d[1];
        if (!a.hasChunkAt(ncx * CHUNK_SIZE, ncz * CHUNK_SIZE)) continue;
        for (int i = 0; i < CHUNK_SIZE; ++i) {
            int ix = d[0] == 1 ? bx + CHUNK_SIZE - 1 : d[0] == -1 ? bx : bx + i;
            int iz = d[1] == 1 ? bz + CHUNK_SIZE - 1 : d[1] == -1 ? bz : bz + i;
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                glm::ivec3 in(ix, y, iz);
                glm::ivec3 out(ix + d[0], y, iz + d[1]);
                for (const glm::ivec3& p : {in, out}) {
                    if (lightAt(a, Channel::Sun, p.x, p.y, p.z) > 1) sunSeeds.push_back(p);
                    if (lightAt(a, Channel::Block, p.x, p.y, p.z) > 1) blockSeeds.push_back(p);
                }
            }
        }
    }
    addLight(a, Channel::Sun, std::move(sunSeeds));
    addLight(a, Channel::Block, std::move(blockSeeds));
}

void computeInitialLight(Chunk& c) {
    constexpr int CS = CHUNK_SIZE;
    for (int y = 0; y < CHUNK_HEIGHT; ++y)
        for (int z = 0; z < CS; ++z)
            for (int x = 0; x < CS; ++x) {
                c.setSunLight(x, y, z, 0);
                c.setBlockLight(x, y, z, 0);
            }

    std::vector<int> sunQ, blockQ;
    sunQ.reserve(8192);

    for (int z = 0; z < CS; ++z) {
        for (int x = 0; x < CS; ++x) {
            for (int y = CHUNK_HEIGHT - 1; y >= 0; --y) {
                Block b = c.get(x, y, z);
                if (isOpaque(b) || dimsSunlight(b)) break;
                c.setSunLight(x, y, z, 15);
                sunQ.push_back((y * CS + z) * CS + x);
            }
        }
    }
    for (int y = 0; y < CHUNK_HEIGHT; ++y)
        for (int z = 0; z < CS; ++z)
            for (int x = 0; x < CS; ++x) {
                uint8_t e = lightEmission(c.get(x, y, z));
                if (e) {
                    c.setBlockLight(x, y, z, e);
                    blockQ.push_back((y * CS + z) * CS + x);
                }
            }

    auto spread = [&](std::vector<int>& q, bool sun) {
        for (size_t h = 0; h < q.size(); ++h) {
            int i = q[h];
            int x = i % CS, z = (i / CS) % CS, y = i / (CS * CS);
            uint8_t l = sun ? c.sunLight(x, y, z) : c.blockLight(x, y, z);
            if (l <= 1) continue;
            for (const glm::ivec3& d : LIGHT_DIRS) {
                int nx = x + d.x, ny = y + d.y, nz = z + d.z;
                if (nx < 0 || nx >= CS || nz < 0 || nz >= CS ||
                    ny < 0 || ny >= CHUNK_HEIGHT) continue;
                Block nb = c.get(nx, ny, nz);
                if (isOpaque(nb)) continue;
                uint8_t target = (sun && d.y == -1 && l == 15 &&
                                  !dimsSunlight(nb)) ? 15 : l - 1;
                uint8_t nl = sun ? c.sunLight(nx, ny, nz) : c.blockLight(nx, ny, nz);
                if (nl < target) {
                    if (sun) c.setSunLight(nx, ny, nz, target);
                    else     c.setBlockLight(nx, ny, nz, target);
                    q.push_back((ny * CS + nz) * CS + nx);
                }
            }
        }
    };
    spread(sunQ, true);
    spread(blockQ, false);
}

}
