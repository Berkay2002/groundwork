#include "render/Texture.h"
#include "world/Block.h"
#include "render/GLCompat.h"
#include <cstdint>
#include <vector>

namespace {
constexpr int TILE = 16;

// Small deterministic hash for pixel-level texture noise.
uint32_t pixHash(int x, int y, uint32_t salt) {
    uint32_t h = salt;
    h ^= uint32_t(x) * 0x85EBCA6Bu;
    h = (h << 13) | (h >> 19);
    h ^= uint32_t(y) * 0xC2B2AE35u;
    h *= 0x27D4EB2Fu;
    h ^= h >> 15;
    return h;
}
float noise01(int x, int y, uint32_t salt) {
    return (pixHash(x, y, salt) & 0xFFFF) / 65535.0f;
}

struct RGB { uint8_t r, g, b; };

RGB shade(RGB c, float f) {
    auto cl = [](float v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); };
    return {cl(c.r * f), cl(c.g * f), cl(c.b * f)};
}

RGB grassTopPixel(int x, int y) {
    float n = 0.85f + 0.3f * noise01(x, y, 1);
    return shade({106, 170, 64}, n);
}
RGB dirtPixel(int x, int y) {
    float n = 0.8f + 0.4f * noise01(x, y, 2);
    return shade({134, 96, 67}, n);
}
RGB stonePixel(int x, int y) {
    float n = 0.82f + 0.36f * noise01(x, y, 3);
    // A few darker speckles
    if (noise01(x, y, 4) > 0.92f) n *= 0.75f;
    return shade({130, 130, 130}, n);
}
RGB grassSidePixel(int x, int y) {
    // y=0 is the top row of the tile (v=1 maps to top of the block face in the mesher,
    // and GL row 0 is sampled at v=0, so we flip: top of face = last rows).
    int fromTop = TILE - 1 - y;
    int fringe = 3 + int(noise01(x, 0, 5) * 3.0f); // wavy grass edge
    if (fromTop < fringe) return grassTopPixel(x, y);
    return dirtPixel(x, y);
}
RGB woodSidePixel(int x, int y) {
    // Vertical bark streaks: noise varies mostly with x.
    float n = 0.75f + 0.3f * noise01(x, y / 4, 6) + 0.1f * noise01(x, y, 7);
    return shade({103, 82, 49}, n);
}
RGB woodTopPixel(int x, int y) {
    // Concentric rings around the tile center.
    float dx = x - 7.5f, dy = y - 7.5f;
    float d = dx * dx + dy * dy;
    float ring = 0.85f + 0.25f * float(int(d / 7.0f) % 2);
    return shade({160, 130, 80}, ring * (0.9f + 0.2f * noise01(x, y, 8)));
}
RGB leavesPixel(int x, int y) {
    float n = 0.6f + 0.55f * noise01(x, y, 9);
    if (noise01(x, y, 10) > 0.85f) n *= 0.6f; // dark gaps
    return shade({58, 122, 40}, n);
}
RGB sandPixel(int x, int y) {
    float n = 0.88f + 0.22f * noise01(x, y, 11);
    return shade({219, 207, 163}, n);
}
RGB bedrockPixel(int x, int y) {
    float n = 0.5f + 0.7f * noise01(x / 2, y / 2, 12);
    return shade({80, 80, 84}, n);
}
// Ore tiles: the stone texture with embedded mineral blobs. Blobs come from
// thresholded low-res noise so they cluster into 2-3px nuggets.
bool oreBlob(int x, int y, uint32_t salt) {
    return noise01(x / 2, y / 2, salt) > 0.78f;
}
RGB coalOrePixel(int x, int y) {
    if (oreBlob(x, y, 16)) {
        float n = 0.7f + 0.5f * noise01(x, y, 17);
        return shade({38, 38, 42}, n);
    }
    return stonePixel(x, y);
}
RGB ironOrePixel(int x, int y) {
    if (oreBlob(x, y, 18)) {
        float n = 0.8f + 0.35f * noise01(x, y, 19);
        return shade({216, 168, 122}, n);
    }
    return stonePixel(x, y);
}
RGB waterPixel(int x, int y) {
    // Deep blue with faint horizontal wave streaks.
    float n = 0.85f + 0.2f * noise01(x / 3, y, 20) + 0.1f * noise01(x, y, 21);
    return shade({52, 96, 188}, n);
}
RGB torchPixel(int x, int y) {
    // Classic torch: wooden stick up the middle, flame on top. y counts from
    // the bottom of the face (v=0). The torch model samples the central 2px
    // strip (x 7..8): stick rows 0..5, flame rows 6..9, flame cap rows 8..9.
    bool core = x >= 7 && x <= 8;
    if (core && y <= 5)
        return shade({118, 92, 51}, 0.78f + 0.35f * noise01(x, y, 14)); // stick
    if (core && (y == 7 || y == 8))
        return shade({255, 244, 180}, 0.92f + 0.16f * noise01(x, y, 13)); // hot core
    if (x >= 6 && x <= 9 && y >= 5 && y <= 10)
        return shade({252, 150, 28}, 0.85f + 0.3f * noise01(x, y, 13)); // flame
    return shade({26, 26, 30}, 0.8f + 0.3f * noise01(x, y, 15)); // background
}

// One function per tile; shared by the HUD's 2D atlas strip and the chunk
// renderer's texture array so both show identical art. The switch covers
// every TileId enumerator (no default) so adding a tile without art is a
// compiler warning, and an out-of-range tile renders the magenta Error art
// instead of silently borrowing another block's texture.
RGB tilePixel(TileId tile, int x, int y) {
    switch (tile) {
        case TileId::GrassTop: return grassTopPixel(x, y);
        case TileId::GrassSide: return grassSidePixel(x, y);
        case TileId::Dirt: return dirtPixel(x, y);
        case TileId::Stone: return stonePixel(x, y);
        case TileId::WoodSide: return woodSidePixel(x, y);
        case TileId::WoodTop: return woodTopPixel(x, y);
        case TileId::Leaves: return leavesPixel(x, y);
        case TileId::Sand: return sandPixel(x, y);
        case TileId::Bedrock: return bedrockPixel(x, y);
        case TileId::Torch: return torchPixel(x, y);
        case TileId::CoalOre: return coalOrePixel(x, y);
        case TileId::IronOre: return ironOrePixel(x, y);
        case TileId::Water: return waterPixel(x, y);
        case TileId::Error:
        case TileId::Count: break;
    }
    return {255, 0, 255};
}
}

unsigned createBlockAtlas() {
    const int W = TILE * ATLAS_TILES, H = TILE;
    std::vector<uint8_t> img(size_t(W) * H * 3);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            RGB c = tilePixel(TileId(x / TILE), x % TILE, y);
            size_t i = (size_t(y) * W + x) * 3;
            img[i] = c.r; img[i + 1] = c.g; img[i + 2] = c.b;
        }
    }

    unsigned tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, W, H, 0, GL_RGB, GL_UNSIGNED_BYTE, img.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

unsigned createBlockTextureArray() {
    // Same tiles as the atlas, one per array layer, with REPEAT wrapping so
    // greedy-merged faces can tile their texture (UVs span 0..w / 0..h).
    std::vector<uint8_t> img(size_t(TILE) * TILE * ATLAS_TILES * 3);
    for (int layer = 0; layer < ATLAS_TILES; ++layer)
        for (int y = 0; y < TILE; ++y)
            for (int x = 0; x < TILE; ++x) {
                RGB c = tilePixel(TileId(layer), x, y);
                size_t i = ((size_t(layer) * TILE + y) * TILE + x) * 3;
                img[i] = c.r; img[i + 1] = c.g; img[i + 2] = c.b;
            }

    unsigned tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB8, TILE, TILE, ATLAS_TILES, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, img.data());
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return tex;
}
