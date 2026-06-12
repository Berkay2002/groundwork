#include "ui/InventoryUi.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "sim/Crafting.h"
#include "ui/Hud.h"
#include "render/Texture.h"

namespace ui {

// --- Classic-Minecraft panel style primitives -----------------------------

// Raised bevel: outer dark border, near-white top+left highlight, dark-gray
// bottom+right shadow, light-gray opaque fill.
void drawPanel(Hud& hud, const Rect& r) {
    const float b = 3.0f;   // bevel thickness
    const float o = 1.0f;   // outer border thickness
    // Outer dark border (reads against bright skies).
    hud.drawRect(r.x, r.y, r.w, r.h, 0.13f, 0.13f, 0.13f, 1.0f);
    float ix = r.x + o, iy = r.y + o, iw = r.w - 2 * o, ih = r.h - 2 * o;
    // Light-gray fill.
    hud.drawRect(ix, iy, iw, ih, 0.78f, 0.78f, 0.78f, 1.0f);
    // Dark-gray bottom + right shadow.
    hud.drawRect(ix, iy + ih - b, iw, b, 0.34f, 0.34f, 0.34f, 1.0f);
    hud.drawRect(ix + iw - b, iy, b, ih, 0.34f, 0.34f, 0.34f, 1.0f);
    // Near-white top + left highlight (drawn last so it owns the corner).
    hud.drawRect(ix, iy, iw, b, 0.97f, 0.97f, 0.97f, 1.0f);
    hud.drawRect(ix, iy, b, ih, 0.97f, 0.97f, 0.97f, 1.0f);
}

// Inset bevel: dark top+left, near-white bottom+right, medium-gray center.
void drawBeveledSlot(Hud& hud, const Rect& r) {
    const float b = 2.0f;
    // Near-white bottom+right (full rect first).
    hud.drawRect(r.x, r.y, r.w, r.h, 0.95f, 0.95f, 0.95f, 1.0f);
    // Dark top+left.
    hud.drawRect(r.x, r.y, r.w, b, 0.35f, 0.35f, 0.35f, 1.0f);
    hud.drawRect(r.x, r.y, b, r.h, 0.35f, 0.35f, 0.35f, 1.0f);
    // Medium-gray center.
    hud.drawRect(r.x + b, r.y + b, r.w - 2 * b, r.h - 2 * b,
                 0.55f, 0.55f, 0.55f, 1.0f);
}

// Medium-dark gray arrow pointing right: a shaft plus a stepped-rect head.
void drawArrow(Hud& hud, const Rect& r) {
    const float cr = 0.42f, cg = 0.42f, cb = 0.42f;
    float headW = r.w * 0.45f;
    float shaftW = r.w - headW;
    float shaftH = r.h * 0.34f;
    float sy = r.y + (r.h - shaftH) * 0.5f;
    // Shaft.
    hud.drawRect(r.x, sy, shaftW, shaftH, cr, cg, cb, 1.0f);
    // Stepped head (3 rects narrowing toward the tip).
    float hx = r.x + shaftW;
    float step = headW / 3.0f;
    for (int i = 0; i < 3; ++i) {
        float frac = 1.0f - i / 3.0f;          // 1.0, 0.667, 0.333
        float h = r.h * frac;
        float y = r.y + (r.h - h) * 0.5f;
        hud.drawRect(hx + i * step, y, step + 0.5f, h, cr, cg, cb, 1.0f);
    }
}

void drawItemStack(Hud& hud, const ItemStack& s, float x, float y, float size,
                   float brightness) {
    if (s.empty()) return;
    Block b = placeBlockForItem(s.item);
    if (b != Block::Air) hud.drawTile(x, y, size, tileFor(b, 4), brightness);
    else hud.drawTile(x, y, size, int(itemIconTile(s.item)), brightness);

    const ItemDef& d = itemDef(s.item);
    if (d.maxDurability > 0) {
        float ratio = float(s.durability) / float(d.maxDurability);
        hud.drawRect(x, y + size - 5.0f, size, 4.0f, 0.05f, 0.05f, 0.05f, 0.85f);
        hud.drawRect(x + 1.0f, y + size - 4.0f, (size - 2.0f) * ratio, 2.0f,
                     0.2f, 0.85f, 0.2f, 0.95f);
    } else if (s.count > 1 || d.stackMax > 1) {
        char cnt[4];
        std::snprintf(cnt, sizeof(cnt), "%d", s.count);
        float cw = std::strlen(cnt) * Hud::GLYPH * 1.5f;
        float tx = x + size - cw - 2.0f, ty = y + size - 14.0f;
        // Subtle dark shadow keeps white count legible on light slot centers.
        hud.drawText(tx + 1.0f, ty + 1.0f, 1.5f, cnt, 0.1f, 0.1f, 0.1f, 0.85f);
        hud.drawText(tx, ty, 1.5f, cnt);
    }
}

void drawHotbar(Hud& hud, const HotbarView& view, int screenW, int screenH) {
    const float slot = 56.0f, pad = 4.0f, icon = slot - 2 * pad;
    const int slots = view.paletteCount;
    float totalW = slots * slot + (slots - 1) * pad;
    float x0 = (screenW - totalW) * 0.5f;
    float y = screenH - slot - 12.0f;
    for (int i = 0; i < slots; ++i) {
        float x = x0 + i * (slot + pad);
        bool sel = (i == view.hotbarSlot);
        if (sel) hud.drawRect(x - 3, y - 3, slot + 6, slot + 6, 1, 1, 1, 0.9f);
        hud.drawRect(x, y, slot, slot, 0.1f, 0.1f, 0.1f, 0.65f);
        // Icon: the side texture for grass, base tile otherwise. Survival
        // shows the inventory's hotbar row with stack counts instead of the
        // fixed creative palette.
        if (view.survival) {
            const ItemStack& s = view.inv.slots[i];
            drawItemStack(hud, s, x + pad, y + pad, icon, sel ? 1.0f : 0.8f);
        } else {
            hud.drawTile(x + pad, y + pad, icon, tileFor(view.palette[i], 4),
                         sel ? 1.0f : 0.8f);
        }
        char num[2] = {char('1' + i), 0};
        hud.drawText(x + 4, y + 4, 1.0f, num, 1, 1, 1, 0.8f);
    }
    if (view.heldName && view.heldName[0] != '\0') {
        float nameW = std::strlen(view.heldName) * Hud::GLYPH * 2.0f;
        hud.drawText((screenW - nameW) * 0.5f, y - 26.0f, 2.0f, view.heldName);
    }
}

void drawHearts(Hud& hud, int health, int maxHealth, int screenW, int screenH) {
    // 7x6-pixel heart as per-row horizontal spans [start, end).
    struct RowSpans { int count; int span[2][2]; };
    static constexpr RowSpans HEART[6] = {
        {2, {{1, 3}, {4, 6}}},
        {1, {{0, 7}, {0, 0}}},
        {1, {{0, 7}, {0, 0}}},
        {1, {{1, 6}, {0, 0}}},
        {1, {{2, 5}, {0, 0}}},
        {1, {{3, 4}, {0, 0}}},
    };
    const float slot = 56.0f, pad = 4.0f;
    const float px = 3.0f;                  // heart "pixel" size
    const float step = 7 * px + 3.0f;       // heart width + gap
    // Anchor to the hotbar: same left edge, one row above it.
    float totalW = 9 * slot + 8 * pad;
    float x0 = (screenW - totalW) * 0.5f;
    float y0 = (screenH - slot - 12.0f) - 6 * px - 8.0f;
    int hearts = (maxHealth + 1) / 2;
    for (int i = 0; i < hearts; ++i) {
        int hp = std::max(0, std::min(2, health - 2 * i));
        // Filled width: whole heart, the left ~half, or nothing.
        int fillCols = hp == 2 ? 7 : hp == 1 ? 4 : 0;
        float hx = x0 + i * step;
        for (int row = 0; row < 6; ++row) {
            for (int s = 0; s < HEART[row].count; ++s) {
                int a = HEART[row].span[s][0], b = HEART[row].span[s][1];
                hud.drawRect(hx + a * px, y0 + row * px, (b - a) * px, px,
                             0.18f, 0.03f, 0.03f, 0.9f); // empty backing
                int fb = std::min(b, fillCols);
                if (fb > a)
                    hud.drawRect(hx + a * px, y0 + row * px, (fb - a) * px, px,
                                 0.86f, 0.12f, 0.12f, 1.0f);
            }
        }
    }
}

void drawInventoryScreen(Hud& hud, const InventoryView& view, int screenW,
                         int screenH) {
    hud.drawRect(0, 0, float(screenW), float(screenH), 0, 0, 0, 0.55f);
    ui::InventoryLayout L = ui::inventoryLayout(screenW, screenH);
    bool furnace = (view.screen == ScreenKind::Furnace);
    ui::InventorySurface surf =
        furnace ? ui::InventorySurface::Furnace : ui::InventorySurface::Crafting;
    int craftSurface = furnace ? 0 : view.crafting.grid.width;

    // Main panel.
    drawPanel(hud, ui::panelRect(L, surf, craftSurface));
    // Recipe-reference side panel (crafting surfaces only).
    if (!furnace) drawPanel(hud, ui::recipePanelRect(L));

    // Title text, top-left inside the panel, dark gray.
    const char* title = furnace ? "Furnace"
                       : (craftSurface >= 3 ? "Crafting" : "Inventory");
    hud.drawText(L.panelX + L.margin, L.panelY + L.margin, 2.0f, title,
                 0.25f, 0.25f, 0.25f, 1.0f);

    // Main 3x9 inventory grid + hotbar row.
    for (int i = 0; i < Inventory::SLOTS; ++i) {
        ui::Rect r = ui::inventorySlotRect(L, i);
        drawBeveledSlot(hud, r);
        drawItemStack(hud, view.inv.slots[i], r.x + L.pad, r.y + L.pad,
                      L.slot - 2 * L.pad);
    }

    if (furnace) {
        FurnaceState* f = view.furnace;
        for (ui::FurnaceSlot slot : {ui::FurnaceSlot::Input, ui::FurnaceSlot::Fuel,
                                     ui::FurnaceSlot::Output}) {
            ui::Rect r = ui::furnaceSlotRect(L, slot);
            drawBeveledSlot(hud, r);
            if (f) drawItemStack(hud, ui::furnaceSlotRef(*f, slot),
                                 r.x + L.pad, r.y + L.pad, L.slot - 2 * L.pad);
        }
        // Arrow from input toward the output slot.
        drawArrow(hud, ui::arrowRect(L, surf, 0));
        // Flame indicator beside the fuel slot.
        if (f && f->burnTicksRemaining > 0)
            hud.drawRect(ui::furnaceSlotRect(L, ui::FurnaceSlot::Fuel).x + L.slot + 10.0f,
                         ui::furnaceSlotRect(L, ui::FurnaceSlot::Fuel).y + 8.0f,
                         8.0f, 34.0f, 0.95f, 0.45f, 0.12f, 1.0f);
        // Cook-progress bar below the arrow.
        if (f && f->cookTicks > 0) {
            ui::Rect a = ui::arrowRect(L, surf, 0);
            hud.drawRect(a.x, a.y + a.h + 4.0f,
                         a.w * (float(f->cookTicks) / 200.0f), 6.0f,
                         0.9f, 0.9f, 0.4f, 1.0f);
        }
    } else {
        int surface = craftSurface;
        for (int y = 0; y < surface; ++y)
            for (int x = 0; x < surface; ++x) {
                ui::Rect r = ui::craftSlotRect(L, surface, x, y);
                drawBeveledSlot(hud, r);
                drawItemStack(hud, view.crafting.grid.at(x, y),
                              r.x + L.pad, r.y + L.pad, L.slot - 2 * L.pad);
            }
        // Arrow from the craft grid toward the output slot.
        drawArrow(hud, ui::arrowRect(L, surf, surface));
        ui::Rect out = ui::craftOutputRect(L, surface);
        drawBeveledSlot(hud, out);
        drawItemStack(hud, crafting::craftingOutput(view.crafting.grid),
                      out.x + L.pad, out.y + L.pad, L.slot - 2 * L.pad);
        std::vector<ItemStack> recipes = ui::recipeReferenceOutputs();
        for (size_t i = 0; i < recipes.size(); ++i) {
            ui::Rect r = ui::recipeReferenceSlotRect(L, int(i));
            drawBeveledSlot(hud, r);
            drawItemStack(hud, recipes[i], r.x + 3.0f, r.y + 3.0f, 24.0f, 0.9f);
        }
    }

    // Armor placeholder column + dark inset player-preview box (inventory
    // screen only). The armor slots are decorative: hit-testing skips them.
    if (view.screen == ScreenKind::Inventory) {
        for (int i = 0; i < ui::ARMOR_SLOTS; ++i)
            drawBeveledSlot(hud, ui::armorSlotRect(L, i));
        ui::Rect box = ui::playerBoxRect(L);
        drawBeveledSlot(hud, box);  // bevel frame
        hud.drawRect(box.x + 2.0f, box.y + 2.0f, box.w - 4.0f, box.h - 4.0f,
                     0.05f, 0.05f, 0.05f, 1.0f);
    }
    if (!view.cursorStack.empty()) { // stack riding the mouse
        drawItemStack(hud, view.cursorStack, view.mouseX - 20, view.mouseY - 20, 40);
    }
}

} // namespace ui
