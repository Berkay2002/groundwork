#include "render/Texture.h"
#include "world/Block.h"
#include "render/GLCompat.h"
#include "render/BreakOverlay.h"
#include <algorithm>
#include <cstdlib>
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
RGB diamondOrePixel(int x, int y) {
    if (oreBlob(x, y, 22)) {
        float n = 0.85f + 0.25f * noise01(x, y, 23);
        return shade({74, 220, 212}, n);
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
RGB cobblestonePixel(int x, int y) {
    int bx = x / 4, by = y / 4;
    float mortar = (x % 4 == 0 || y % 4 == 0) ? 0.65f : 1.0f;
    float n = 0.75f + 0.35f * noise01(bx, by, 24) + 0.12f * noise01(x, y, 25);
    return shade({118, 118, 118}, n * mortar);
}
RGB planksPixel(int x, int y) {
    bool seam = (y == 4 || y == 9 || y == 14);
    float n = 0.82f + 0.24f * noise01(x / 3, y, 26) + 0.12f * noise01(x, y, 27);
    if (seam) n *= 0.65f;
    return shade({152, 110, 62}, n);
}
RGB craftingTableSidePixel(int x, int y) {
    if (y < 3 || y > 12 || x < 2 || x > 13) return woodSidePixel(x, y);
    if ((x + y) % 5 == 0) return shade({86, 62, 38}, 0.9f);
    return planksPixel(x, y);
}
RGB craftingTableTopPixel(int x, int y) {
    bool grid = x == 5 || x == 10 || y == 5 || y == 10;
    float n = 0.85f + 0.25f * noise01(x, y, 28);
    return shade(grid ? RGB{80, 54, 32} : RGB{166, 120, 68}, n);
}
RGB furnaceSidePixel(int x, int y) {
    return cobblestonePixel(x, y);
}
RGB furnaceFrontPixel(int x, int y) {
    if (x >= 4 && x <= 11 && y >= 5 && y <= 10) {
        float n = 0.7f + 0.25f * noise01(x, y, 29);
        return shade({46, 46, 48}, n);
    }
    return cobblestonePixel(x, y);
}

RGB iconBg(int x, int y) {
    bool border = x == 0 || y == 0 || x == TILE - 1 || y == TILE - 1;
    float n = 0.85f + 0.12f * noise01(x, y, 30);
    return shade(border ? RGB{25, 26, 28} : RGB{48, 50, 54}, n);
}
RGB materialColor(ToolTier tier) {
    switch (tier) {
        case ToolTier::Wood: return {150, 100, 55};
        case ToolTier::Stone: return {130, 130, 132};
        case ToolTier::Iron: return {215, 218, 210};
        case ToolTier::Diamond: return {74, 220, 212};
        case ToolTier::Hand: return {180, 140, 90};
    }
    return {255, 0, 255};
}
RGB itemStickPixel(int x, int y) {
    if (std::abs(x - 8) <= 1 && y >= 3 && y <= 13)
        return shade({130, 82, 42}, 0.85f + 0.2f * noise01(x, y, 31));
    return iconBg(x, y);
}
RGB itemLumpPixel(int x, int y, RGB color, uint32_t salt) {
    int dx = x - 8, dy = y - 8;
    if (dx * dx + dy * dy < 30 + int(noise01(x, y, salt) * 8.0f))
        return shade(color, 0.75f + 0.35f * noise01(x, y, salt + 1));
    return iconBg(x, y);
}
RGB itemIngotPixel(int x, int y, RGB color, uint32_t salt) {
    bool body = y >= 6 && y <= 10 && x >= 3 && x <= 12;
    bool bevel = (x == 3 || x == 12) && (y == 6 || y == 10);
    if (body && !bevel) return shade(color, 0.8f + 0.25f * noise01(x, y, salt));
    return iconBg(x, y);
}
RGB itemDiamondPixel(int x, int y) {
    int dx = std::abs(x - 8), dy = std::abs(y - 8);
    if (dx + dy <= 6) return shade({74, 220, 212}, 0.85f + 0.25f * noise01(x, y, 36));
    return iconBg(x, y);
}
RGB toolIconPixel(int x, int y, ToolClass cls, ToolTier tier) {
    RGB head = materialColor(tier);
    bool handle = std::abs((x + y) - 17) <= 1 && x >= 5 && x <= 11 && y >= 6 && y <= 13;
    bool toolHead = false;
    if (cls == ToolClass::Pickaxe) toolHead = y >= 3 && y <= 5 && x >= 3 && x <= 13;
    if (cls == ToolClass::Axe) toolHead = x >= 4 && x <= 9 && y >= 3 && y <= 8 && x + y <= 14;
    if (cls == ToolClass::Shovel) {
        int dx = x - 8, dy = y - 4;
        toolHead = dx * dx + dy * dy <= 10 && y <= 8;
    }
    if (toolHead) return shade(head, 0.8f + 0.25f * noise01(x, y, 37 + tierLevel(tier)));
    if (handle) return shade({116, 76, 38}, 0.85f + 0.15f * noise01(x, y, 42));
    return iconBg(x, y);
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
        case TileId::Cobblestone: return cobblestonePixel(x, y);
        case TileId::Planks: return planksPixel(x, y);
        case TileId::CraftingTableSide: return craftingTableSidePixel(x, y);
        case TileId::CraftingTableTop: return craftingTableTopPixel(x, y);
        case TileId::FurnaceSide: return furnaceSidePixel(x, y);
        case TileId::FurnaceFront: return furnaceFrontPixel(x, y);
        case TileId::DiamondOre: return diamondOrePixel(x, y);
        case TileId::ItemStick: return itemStickPixel(x, y);
        case TileId::ItemCoal: return itemLumpPixel(x, y, {32, 31, 30}, 32);
        case TileId::ItemRawIron: return itemLumpPixel(x, y, {196, 118, 70}, 34);
        case TileId::ItemIronIngot: return itemIngotPixel(x, y, {215, 218, 210}, 35);
        case TileId::ItemDiamond: return itemDiamondPixel(x, y);
        case TileId::ItemWoodPickaxe: return toolIconPixel(x, y, ToolClass::Pickaxe, ToolTier::Wood);
        case TileId::ItemWoodAxe: return toolIconPixel(x, y, ToolClass::Axe, ToolTier::Wood);
        case TileId::ItemWoodShovel: return toolIconPixel(x, y, ToolClass::Shovel, ToolTier::Wood);
        case TileId::ItemStonePickaxe: return toolIconPixel(x, y, ToolClass::Pickaxe, ToolTier::Stone);
        case TileId::ItemStoneAxe: return toolIconPixel(x, y, ToolClass::Axe, ToolTier::Stone);
        case TileId::ItemStoneShovel: return toolIconPixel(x, y, ToolClass::Shovel, ToolTier::Stone);
        case TileId::ItemIronPickaxe: return toolIconPixel(x, y, ToolClass::Pickaxe, ToolTier::Iron);
        case TileId::ItemIronAxe: return toolIconPixel(x, y, ToolClass::Axe, ToolTier::Iron);
        case TileId::ItemIronShovel: return toolIconPixel(x, y, ToolClass::Shovel, ToolTier::Iron);
        case TileId::ItemDiamondPickaxe: return toolIconPixel(x, y, ToolClass::Pickaxe, ToolTier::Diamond);
        case TileId::ItemDiamondAxe: return toolIconPixel(x, y, ToolClass::Axe, ToolTier::Diamond);
        case TileId::ItemDiamondShovel: return toolIconPixel(x, y, ToolClass::Shovel, ToolTier::Diamond);
        case TileId::Error:
        case TileId::Count: break;
    }
    return {255, 0, 255};
}

float distanceToSegment(float px, float py, float ax, float ay, float bx, float by) {
    float vx = bx - ax, vy = by - ay;
    float wx = px - ax, wy = py - ay;
    float len2 = vx * vx + vy * vy;
    float t = len2 > 0.0f ? (wx * vx + wy * vy) / len2 : 0.0f;
    t = std::max(0.0f, std::min(1.0f, t));
    float dx = px - (ax + vx * t), dy = py - (ay + vy * t);
    return dx * dx + dy * dy;
}

uint8_t crackAlphaPixel(int stage, int x, int y) {
    struct Segment { float ax, ay, bx, by; int unlock; };
    constexpr Segment segments[] = {
        {8.0f, 8.0f, 8.0f, 4.0f, 0},  {8.0f, 8.0f, 5.0f, 6.0f, 1},
        {8.0f, 8.0f, 11.0f, 6.0f, 2}, {8.0f, 4.0f, 6.0f, 2.0f, 3},
        {5.0f, 6.0f, 3.0f, 9.0f, 4}, {11.0f, 6.0f, 13.0f, 9.0f, 5},
        {8.0f, 8.0f, 8.0f, 12.0f, 6}, {8.0f, 12.0f, 5.0f, 14.0f, 7},
        {8.0f, 12.0f, 12.0f, 14.0f, 8}, {3.0f, 9.0f, 1.0f, 12.0f, 9},
    };
    float px = float(x) + 0.5f, py = float(y) + 0.5f;
    float best = 1000.0f;
    for (const Segment& s : segments) {
        if (stage < s.unlock) continue;
        best = std::min(best, distanceToSegment(px, py, s.ax, s.ay, s.bx, s.by));
    }
    float width = 0.24f + 0.045f * float(stage);
    if (best <= width) return uint8_t(225);
    if (best <= width + 0.9f && stage >= 4) return uint8_t(95);
    return 0;
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

unsigned createBreakTextureArray() {
    std::vector<uint8_t> img(size_t(TILE) * TILE * BREAK_CRACK_STAGES * 4, 0);
    for (int layer = 0; layer < BREAK_CRACK_STAGES; ++layer)
        for (int y = 0; y < TILE; ++y)
            for (int x = 0; x < TILE; ++x) {
                size_t i = ((size_t(layer) * TILE + y) * TILE + x) * 4;
                img[i] = img[i + 1] = img[i + 2] = 0;
                img[i + 3] = crackAlphaPixel(layer, x, y);
            }

    unsigned tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TILE, TILE, BREAK_CRACK_STAGES, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img.data());
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}
