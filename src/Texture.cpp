#include "Texture.h"
#include "Block.h"
#include <GL/gl.h>
#include <GL/glext.h>
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
}

unsigned createBlockAtlas() {
    const int W = TILE * ATLAS_TILES, H = TILE;
    std::vector<uint8_t> img(size_t(W) * H * 3);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int tile = x / TILE;
            int tx = x % TILE;
            RGB c;
            switch (tile) {
                case 0: c = grassTopPixel(tx, y); break;
                case 1: c = grassSidePixel(tx, y); break;
                case 2: c = dirtPixel(tx, y); break;
                case 3: c = stonePixel(tx, y); break;
                case 4: c = woodSidePixel(tx, y); break;
                case 5: c = woodTopPixel(tx, y); break;
                case 6: c = leavesPixel(tx, y); break;
                case 7: c = sandPixel(tx, y); break;
                case 8: c = bedrockPixel(tx, y); break;
                case 9: c = torchPixel(tx, y); break;
                case 10: c = coalOrePixel(tx, y); break;
                case 11: c = ironOrePixel(tx, y); break;
                default: c = waterPixel(tx, y); break;
            }
            size_t i = (size_t(y) * W + x) * 3;
            img[i] = c.r; img[i + 1] = c.g; img[i + 2] = c.b;
        }
    }

    unsigned tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, W, H, 0, GL_RGB, GL_UNSIGNED_BYTE, img.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}
