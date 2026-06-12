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

// Tile-space orientation: y = 0 is the VISUAL BOTTOM of a tile (the HUD
// flips v in drawTile, and the mesher maps v=1 to the top of a block face).
// Sprite maps below are authored top-down (row 0 = visual top) because
// that's how pixel art reads, and the lookup flips: rows[TILE-1-y][x].

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
struct RGBA { uint8_t r, g, b, a; };

RGB shade(RGB c, float f) {
    auto cl = [](float v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); };
    return {cl(c.r * f), cl(c.g * f), cl(c.b * f)};
}
RGBA opaque(RGB c) { return {c.r, c.g, c.b, 255}; }

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
// thresholded low-res noise so they cluster into 2-3px nuggets; stone pixels
// touching a blob darken slightly so the nuggets look set into the rock.
bool oreBlob(int x, int y, uint32_t salt) {
    return noise01(x / 2, y / 2, salt) > 0.78f;
}
RGB orePixel(int x, int y, uint32_t salt, RGB mineral) {
    if (oreBlob(x, y, salt)) {
        float n = 0.78f + 0.4f * noise01(x, y, salt + 1);
        return shade(mineral, n);
    }
    bool edge = oreBlob(x - 1, y, salt) || oreBlob(x + 1, y, salt) ||
                oreBlob(x, y - 1, salt) || oreBlob(x, y + 1, salt);
    RGB s = stonePixel(x, y);
    return edge ? shade(s, 0.78f) : s;
}
RGB waterPixel(int x, int y) {
    // Deep blue with faint horizontal wave streaks.
    float n = 0.85f + 0.2f * noise01(x / 3, y, 20) + 0.1f * noise01(x, y, 21);
    return shade({52, 96, 188}, n);
}
RGBA torchPixel(int x, int y) {
    // Classic torch: wooden stick up the middle, flame on top. y counts from
    // the bottom of the face (v=0). The torch model samples the central 2px
    // strip (x 7..8): stick rows 0..5, flame rows 6..9, flame cap rows 8..9.
    // The background is transparent so the hotbar/dropped icon is a cut-out;
    // the 3D torch post never samples it.
    bool core = x >= 7 && x <= 8;
    if (core && y <= 5)
        return opaque(shade({118, 92, 51}, 0.78f + 0.35f * noise01(x, y, 14))); // stick
    if (core && (y == 7 || y == 8))
        return opaque(shade({255, 244, 180}, 0.92f + 0.16f * noise01(x, y, 13))); // hot core
    if (x >= 6 && x <= 9 && y >= 5 && y <= 10)
        return opaque(shade({252, 150, 28}, 0.85f + 0.3f * noise01(x, y, 13))); // flame
    return {26, 26, 30, 0}; // background (RGB kept for the opaque array path)
}
RGB cobblestonePixel(int x, int y) {
    // Irregular rounded stones: 4px cells staggered every other row, strong
    // per-stone brightness, darker mortar, corner pixels knocked down so the
    // stones read as rounded instead of a flat grid.
    int by = y / 4;
    int xs = x + ((by & 1) ? 2 : 0);
    int bx = xs / 4, lx = xs % 4, ly = y % 4;
    bool mortar = lx == 0 || ly == 0;
    bool corner = (lx == 1 || lx == 3) && (ly == 1 || ly == 3);
    float stone = 0.78f + 0.4f * noise01(bx, by, 24);
    float n = 0.9f + 0.2f * noise01(x, y, 25);
    float m = mortar ? 0.55f : corner ? 0.85f : 1.0f;
    return shade({125, 123, 121}, stone * n * m);
}
RGB planksPixel(int x, int y) {
    bool seam = (y == 4 || y == 9 || y == 14);
    float n = 0.82f + 0.24f * noise01(x / 3, y, 26) + 0.12f * noise01(x, y, 27);
    if (seam) n *= 0.65f;
    return shade({152, 110, 62}, n);
}
RGB darkWoodPixel(int x, int y, uint32_t salt) {
    return shade({52, 38, 24}, 0.85f + 0.25f * noise01(x, y, salt));
}
RGB craftingTableSidePixel(int x, int y) {
    // Classic crafting-table side (papercraft reference): light planks, a
    // dark top band, dark vertical straps near the edges, and two hanging
    // tools (wood handles, steel blades) between them. Authored top-down.
    static const char* const ROWS[TILE] = {
        "TTTTTTTTTTTTTTTT",
        ".TT..........TT.",
        ".TT..w....w..TT.",
        ".TT..w....w..TT.",
        ".TT..ws...ws.TT.",
        ".TT..ws...ws.TT.",
        ".TT..ss...ss.TT.",
        ".TT..ss...ss.TT.",
        ".TT...s....s.TT.",
        ".TT..........TT.",
        ".TT..........TT.",
        ".TT..........TT.",
        ".TT..........TT.",
        ".TT..........TT.",
        ".TT..........TT.",
        ".TT..........TT.",
    };
    switch (ROWS[TILE - 1 - y][x]) {
        case 'T': return darkWoodPixel(x, y, 66);
        case 's': return shade({208, 211, 216}, 0.85f + 0.25f * noise01(x, y, 67));
        case 'w': return shade({96, 66, 38}, 0.85f + 0.25f * noise01(x, y, 68));
        default: return planksPixel(x, y);
    }
}
RGB craftingTableTopPixel(int x, int y) {
    // Worktop (papercraft reference): plank ring, dark corner accents, and a
    // centered waffle grid of reddish-brown cells. Authored top-down.
    static const char* const ROWS[TILE] = {
        "GGGGGGGGGGGGGGGG",
        "GG............GG",
        "G..............G",
        "G..GGGGGGGGGG..G",
        "G..GccGccGccG..G",
        "G..GccGccGccG..G",
        "G..GGGGGGGGGG..G",
        "G..GccGccGccG..G",
        "G..GccGccGccG..G",
        "G..GGGGGGGGGG..G",
        "G..GccGccGccG..G",
        "G..GccGccGccG..G",
        "G..GGGGGGGGGG..G",
        "G..............G",
        "GG............GG",
        "GGGGGGGGGGGGGGGG",
    };
    switch (ROWS[TILE - 1 - y][x]) {
        case 'G': return darkWoodPixel(x, y, 69);
        case 'c': return shade({176, 112, 62}, 0.85f + 0.25f * noise01(x, y, 70));
        default: {
            float n = 0.85f + 0.2f * noise01(x / 3, y, 28) + 0.1f * noise01(x, y, 71);
            return shade({186, 142, 84}, n);
        }
    }
}
RGB furnaceSidePixel(int x, int y) {
    // Smooth gray stone slabs, visually distinct from cobblestone: 8px
    // blocks with thin seams and a per-slab brightness.
    int bx = x / 8, by = y / 8;
    bool seam = (x % 8 == 7) || (y % 8 == 7);
    float slab = 0.9f + 0.2f * noise01(bx, by, 60);
    float n = 0.92f + 0.16f * noise01(x, y, 61);
    if (noise01(x, y, 62) > 0.9f) n *= 0.85f;
    return shade({121, 121, 124}, slab * n * (seam ? 0.68f : 1.0f));
}
RGB furnaceFrontPixel(int x, int y, bool lit) {
    // Classic furnace front: a dark vent slot in the upper half and a wide
    // firebox below it. When lit, the firebox bottom holds flickering flame
    // pixels (the lit block also emits light via its registry row).
    // Authored top-down: '#' = opening interior, 'r' = rim.
    static const char* const ROWS[TILE] = {
        "................",
        "................",
        "..rrrrrrrrrrrr..",
        ".r############r.",
        ".r############r.",
        "..rrrrrrrrrrrr..",
        "................",
        "................",
        "...rrrrrrrrrr...",
        "..r##########r..",
        "..r##########r..",
        "..r##########r..",
        "..r##########r..",
        "..r##########r..",
        "...rrrrrrrrrr...",
        "................",
    };
    int row = TILE - 1 - y; // visual row from the top
    char ch = ROWS[row][x];
    if (ch == 'r') return shade({54, 54, 57}, 0.85f + 0.2f * noise01(x, y, 63));
    if (ch == '#') {
        if (lit && row >= 9) {
            // Flame fills the firebox from the bottom up: y rows 2..6 hold
            // the lower opening, flame chance falls off with height.
            float h = float(y - 2); // 0 at the firebox floor
            if (noise01(x, y, 80) > h * 0.22f) {
                bool hot = noise01(x, y, 81) > 0.45f - 0.1f * (4.0f - h);
                return shade(hot ? RGB{255, 214, 84} : RGB{246, 126, 22},
                             0.85f + 0.25f * noise01(x, y, 82));
            }
        }
        return shade({20, 19, 20}, 0.7f + 0.35f * noise01(x, y, 64));
    }
    return furnaceSidePixel(x, y);
}
RGB playerArmPixel(int x, int y) {
    // First-person arm skin: warm brown with darker low-res patches.
    float n = 0.94f + 0.12f * noise01(x, y, 90);
    if (noise01(x / 3, y / 3, 91) > 0.7f) n *= 0.86f;
    return shade({177, 116, 81}, n);
}

// --- Item icon sprites -----------------------------------------------------
// 16x16 ASCII sprite maps, row 0 = visual top, '.' = transparent. Shapes
// follow the classic Minecraft item sprites (diagonal handle to the bottom
// left, tool head at the top right, 1px dark outline).

constexpr RGB OUTLINE = {25, 23, 21};
constexpr RGB HANDLE_LIGHT = {146, 108, 62};
constexpr RGB HANDLE_DARK = {112, 80, 44};

struct ToolPalette { RGB light, mid, dark; };
ToolPalette toolPalette(ToolTier tier) {
    switch (tier) {
        case ToolTier::Wood: return {{172, 126, 76}, {140, 98, 54}, {106, 72, 38}};
        case ToolTier::Stone: return {{160, 160, 160}, {125, 125, 125}, {90, 90, 90}};
        case ToolTier::Iron: return {{238, 240, 242}, {205, 208, 212}, {150, 154, 160}};
        case ToolTier::Diamond: return {{102, 236, 222}, {58, 206, 188}, {26, 150, 138}};
        case ToolTier::Hand: break;
    }
    return {{180, 140, 90}, {150, 110, 60}, {110, 80, 40}};
}

// Palette chars: O outline, M/m/d material light/mid/dark, H/h handle.
RGBA toolSpritePixel(const char* const rows[TILE], int x, int y,
                     ToolTier tier, uint32_t salt) {
    ToolPalette p = toolPalette(tier);
    float n = 0.92f + 0.16f * noise01(x, y, salt);
    switch (rows[TILE - 1 - y][x]) {
        case 'O': return opaque(OUTLINE);
        case 'M': return opaque(shade(p.light, n));
        case 'm': return opaque(shade(p.mid, n));
        case 'd': return opaque(shade(p.dark, n));
        case 'H': return opaque(shade(HANDLE_LIGHT, n));
        case 'h': return opaque(shade(HANDLE_DARK, n));
        default: return {0, 0, 0, 0};
    }
}

RGBA toolIconPixel(int x, int y, ToolClass cls, ToolTier tier) {
    static const char* const PICKAXE[TILE] = {
        "...OOOOOOOO.....",
        "..OmmmmmmmmOO...",
        ".OmMMMMMMMmmmO..",
        ".OmMOOOOOOOmmO..",
        ".OMO....OHhOmO..",
        ".OMO...OHhO.OmO.",
        ".OmO..OHhO..OmO.",
        "..O..OHhO...OmO.",
        "....OHhO....OmO.",
        "...OHhO.....OO..",
        "..OHhO..........",
        ".OHhO...........",
        ".OhhO...........",
        ".OhO............",
        "..O.............",
        "................",
    };
    static const char* const AXE[TILE] = {
        "....OOOOO.......",
        "..OOmmmmmOO.....",
        ".OmmMMMMmmmO....",
        ".OmMMOOOmmmmO...",
        ".OmMO..OOmmO....",
        ".OmmO..OHhOO....",
        "..OO..OHhO......",
        ".....OHhO.......",
        "....OHhO........",
        "...OHhO.........",
        "..OHhO..........",
        ".OHhO...........",
        ".OhhO...........",
        ".OhO............",
        "..O.............",
        "................",
    };
    static const char* const SHOVEL[TILE] = {
        ".......OOOO.....",
        "......OmmmmO....",
        ".....OmMMmmmO...",
        "....OmMMMmmmO...",
        "....OmMMmmmmO...",
        "....OmmmmmmO....",
        ".....OmmmmO.....",
        ".....OHhO.......",
        "....OHhO........",
        "...OHhO.........",
        "..OHhO..........",
        ".OHhO...........",
        ".OhhO...........",
        ".OhO............",
        "..O.............",
        "................",
    };
    const char* const* rows = cls == ToolClass::Pickaxe ? PICKAXE
                              : cls == ToolClass::Axe ? AXE : SHOVEL;
    return toolSpritePixel(rows, x, y, tier, 37u + uint32_t(tierLevel(tier)));
}

// Generic item sprites share palette chars: O outline, a main, A highlight,
// b dark shade.
RGBA itemSpritePixel(const char* const rows[TILE], int x, int y,
                     RGB main, RGB hi, RGB dark, RGB outline, uint32_t salt) {
    float n = 0.92f + 0.16f * noise01(x, y, salt);
    switch (rows[TILE - 1 - y][x]) {
        case 'O': return opaque(outline);
        case 'a': return opaque(shade(main, n));
        case 'A': return opaque(shade(hi, n));
        case 'b': return opaque(shade(dark, n));
        default: return {0, 0, 0, 0};
    }
}

RGBA itemStickPixel(int x, int y) {
    static const char* const ROWS[TILE] = {
        "................",
        "..........OO....",
        ".........OAaO...",
        "........OAaO....",
        ".......OAaO.....",
        "......OAaO......",
        ".....OAaO.......",
        "....OAaO........",
        "...OAaO.........",
        "..OAaO..........",
        "..OaaO..........",
        "..OaO...........",
        "...O............",
        "................",
        "................",
        "................",
    };
    return itemSpritePixel(ROWS, x, y, HANDLE_DARK, HANDLE_LIGHT,
                           {86, 60, 34}, OUTLINE, 31);
}

RGBA itemLumpPixel(int x, int y, RGB main, RGB hi, RGB dark, RGB outline,
                   uint32_t salt) {
    static const char* const ROWS[TILE] = {
        "................",
        "................",
        "................",
        ".....OOOO.......",
        "....OaaaaO......",
        "...OaaAAaaO.....",
        "..OaaAAaaabO....",
        "..OaAAaaaabO....",
        "..OaaaaaabbO....",
        "..OaaaaabbbO....",
        "...OaabbbbO.....",
        "....OObbOO......",
        "......OO........",
        "................",
        "................",
        "................",
    };
    return itemSpritePixel(ROWS, x, y, main, hi, dark, outline, salt);
}

RGBA itemIngotPixel(int x, int y) {
    static const char* const ROWS[TILE] = {
        "................",
        "................",
        "................",
        "................",
        "................",
        "....OOOOOOOO....",
        "...OAAAAAAAAO...",
        "...OAaaaaaabO...",
        "..OAaaaaaaaabO..",
        "..OAaaaaaaaabO..",
        ".OaaaaaaaaaaabO.",
        ".ObbbbbbbbbbbbO.",
        "..OOOOOOOOOOOO..",
        "................",
        "................",
        "................",
    };
    return itemSpritePixel(ROWS, x, y, {212, 215, 218}, {245, 246, 247},
                           {150, 155, 160}, {96, 100, 105}, 35);
}

RGBA itemDiamondPixel(int x, int y) {
    static const char* const ROWS[TILE] = {
        "................",
        "................",
        "................",
        "....OOOOOOO.....",
        "...OAAAAAAbO....",
        "..OAAaaaaabbO...",
        "..OAaaaaaabbO...",
        "...OaaaaabbO....",
        "....OaaabbO.....",
        ".....OabbO......",
        "......ObO.......",
        ".......O........",
        "................",
        "................",
        "................",
        "................",
    };
    return itemSpritePixel(ROWS, x, y, {110, 228, 220}, {225, 255, 252},
                           {52, 180, 172}, {30, 110, 105}, 36);
}

// One function per tile; shared by the HUD's 2D atlas strip and the chunk
// renderer's texture array so both show identical art. The switch covers
// every TileId enumerator (no default) so adding a tile without art is a
// compiler warning, and an out-of-range tile renders the magenta Error art
// instead of silently borrowing another block's texture. Alpha is 255 for
// block tiles; item icons (and the torch icon) use it for cut-out
// backgrounds in the HUD and the dropped/held sprite paths.
RGBA tilePixel(TileId tile, int x, int y) {
    switch (tile) {
        case TileId::GrassTop: return opaque(grassTopPixel(x, y));
        case TileId::GrassSide: return opaque(grassSidePixel(x, y));
        case TileId::Dirt: return opaque(dirtPixel(x, y));
        case TileId::Stone: return opaque(stonePixel(x, y));
        case TileId::WoodSide: return opaque(woodSidePixel(x, y));
        case TileId::WoodTop: return opaque(woodTopPixel(x, y));
        case TileId::Leaves: return opaque(leavesPixel(x, y));
        case TileId::Sand: return opaque(sandPixel(x, y));
        case TileId::Bedrock: return opaque(bedrockPixel(x, y));
        case TileId::Torch: return torchPixel(x, y);
        case TileId::CoalOre: return opaque(orePixel(x, y, 16, {38, 38, 42}));
        case TileId::IronOre: return opaque(orePixel(x, y, 18, {216, 168, 122}));
        case TileId::Water: return opaque(waterPixel(x, y));
        case TileId::Cobblestone: return opaque(cobblestonePixel(x, y));
        case TileId::Planks: return opaque(planksPixel(x, y));
        case TileId::CraftingTableSide: return opaque(craftingTableSidePixel(x, y));
        case TileId::CraftingTableTop: return opaque(craftingTableTopPixel(x, y));
        case TileId::FurnaceSide: return opaque(furnaceSidePixel(x, y));
        case TileId::FurnaceFront: return opaque(furnaceFrontPixel(x, y, false));
        case TileId::FurnaceFrontLit: return opaque(furnaceFrontPixel(x, y, true));
        case TileId::DiamondOre: return opaque(orePixel(x, y, 22, {74, 220, 212}));
        case TileId::PlayerArm: return opaque(playerArmPixel(x, y));
        case TileId::ItemStick: return itemStickPixel(x, y);
        case TileId::ItemCoal:
            return itemLumpPixel(x, y, {44, 44, 48}, {82, 82, 88}, {26, 26, 30},
                                 {14, 14, 16}, 32);
        case TileId::ItemRawIron:
            return itemLumpPixel(x, y, {214, 176, 140}, {238, 210, 176},
                                 {182, 132, 92}, {124, 88, 58}, 34);
        case TileId::ItemIronIngot: return itemIngotPixel(x, y);
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
        case TileId::ItemRottenFlesh:
            return itemLumpPixel(x, y, {152, 74, 58}, {196, 118, 86},
                                 {108, 46, 40}, {58, 26, 22}, 37);
        case TileId::Error:
        case TileId::Count: break;
    }
    return {255, 0, 255, 255};
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

// Minecraft-style damage stages: hard-edged pixel cracks that lengthen and
// widen per stage, plus crumble speckles that densify near the cracks. No
// anti-aliased halo — alpha is quantized to two levels so the overlay reads
// as chipped pixels, not a smudge.
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
    float best = 1000.0f; // squared distance to the nearest unlocked crack
    for (const Segment& s : segments) {
        if (stage < s.unlock) continue;
        best = std::min(best, distanceToSegment(px, py, s.ax, s.ay, s.bx, s.by));
    }
    if (best <= 0.32f + 0.05f * float(stage)) return 235;
    // Crumble speckles: per-stage noise so the damage churns as it grows.
    float density = 0.05f + 0.05f * float(stage);
    float prox = best < 6.0f ? 1.0f : 6.0f / best;
    if (noise01(x, y, 200u + uint32_t(stage)) < density * prox) return 165;
    return 0;
}
}

unsigned createBlockAtlas() {
    const int W = TILE * ATLAS_TILES, H = TILE;
    std::vector<uint8_t> img(size_t(W) * H * 4);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            RGBA c = tilePixel(TileId(x / TILE), x % TILE, y);
            size_t i = (size_t(y) * W + x) * 4;
            img[i] = c.r; img[i + 1] = c.g; img[i + 2] = c.b; img[i + 3] = c.a;
        }
    }

    unsigned tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

unsigned createBlockTextureArray() {
    // Same tiles as the atlas, one per array layer, with REPEAT wrapping so
    // greedy-merged faces can tile their texture (UVs span 0..w / 0..h).
    // RGBA so the item-entity/held-item sprite path can alpha-discard; the
    // chunk shader samples .rgb and ignores it.
    std::vector<uint8_t> img(size_t(TILE) * TILE * ATLAS_TILES * 4);
    for (int layer = 0; layer < ATLAS_TILES; ++layer)
        for (int y = 0; y < TILE; ++y)
            for (int x = 0; x < TILE; ++x) {
                RGBA c = tilePixel(TileId(layer), x, y);
                size_t i = ((size_t(layer) * TILE + y) * TILE + x) * 4;
                img[i] = c.r; img[i + 1] = c.g; img[i + 2] = c.b; img[i + 3] = c.a;
            }

    unsigned tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TILE, TILE, ATLAS_TILES, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img.data());
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
