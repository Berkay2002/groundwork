#include "ui/InventoryUi.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "sim/Crafting.h"
#include "ui/Hud.h"
#include "render/Texture.h"

namespace ui {

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
        hud.drawText(x + size - cw - 2.0f, y + size - 14.0f, 1.5f, cnt);
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

void drawInventoryScreen(Hud& hud, const InventoryView& view, int screenW,
                         int screenH) {
    hud.drawRect(0, 0, float(screenW), float(screenH), 0, 0, 0, 0.55f);
    ui::InventoryLayout L = ui::inventoryLayout(screenW, screenH);
    hud.drawText(L.x0, L.y0 - 26.0f, 2.0f, "Inventory");
    for (int i = 0; i < Inventory::SLOTS; ++i) {
        ui::Rect r = ui::inventorySlotRect(L, i);
        hud.drawRect(r.x, r.y, r.w, r.h, 0.15f, 0.15f, 0.15f, 0.9f);
        drawItemStack(hud, view.inv.slots[i], r.x + L.pad, r.y + L.pad,
                      L.slot - 2 * L.pad);
    }
    if (view.screen == ScreenKind::Furnace) {
        FurnaceState* f = view.furnace;
        for (ui::FurnaceSlot slot : {ui::FurnaceSlot::Input, ui::FurnaceSlot::Fuel,
                                     ui::FurnaceSlot::Output}) {
            ui::Rect r = ui::furnaceSlotRect(L, slot);
            hud.drawRect(r.x, r.y, r.w, r.h, 0.16f, 0.16f, 0.16f, 0.92f);
            if (f) drawItemStack(hud, ui::furnaceSlotRef(*f, slot),
                                 r.x + L.pad, r.y + L.pad, L.slot - 2 * L.pad);
        }
        if (f && f->burnTicksRemaining > 0)
            hud.drawRect(ui::furnaceSlotRect(L, ui::FurnaceSlot::Fuel).x + L.slot + 10.0f,
                         ui::furnaceSlotRect(L, ui::FurnaceSlot::Fuel).y + 8.0f,
                         8.0f, 34.0f, 0.95f, 0.45f, 0.12f, 0.9f);
        if (f && f->cookTicks > 0)
            hud.drawRect(ui::furnaceSlotRect(L, ui::FurnaceSlot::Output).x - 42.0f,
                         ui::furnaceSlotRect(L, ui::FurnaceSlot::Output).y + 24.0f,
                         34.0f * (float(f->cookTicks) / 200.0f), 8.0f,
                         0.8f, 0.8f, 0.8f, 0.9f);
    } else {
        int surface = view.crafting.grid.width;
        for (int y = 0; y < surface; ++y)
            for (int x = 0; x < surface; ++x) {
                ui::Rect r = ui::craftSlotRect(L, surface, x, y);
                hud.drawRect(r.x, r.y, r.w, r.h, 0.16f, 0.16f, 0.16f, 0.92f);
                drawItemStack(hud, view.crafting.grid.at(x, y),
                              r.x + L.pad, r.y + L.pad, L.slot - 2 * L.pad);
            }
        ui::Rect out = ui::craftOutputRect(L, surface);
        hud.drawRect(out.x, out.y, out.w, out.h, 0.22f, 0.22f, 0.16f, 0.92f);
        drawItemStack(hud, crafting::craftingOutput(view.crafting.grid),
                      out.x + L.pad, out.y + L.pad, L.slot - 2 * L.pad);
        std::vector<ItemStack> recipes = ui::recipeReferenceOutputs();
        for (size_t i = 0; i < recipes.size(); ++i) {
            ui::Rect r = ui::recipeReferenceSlotRect(L, int(i));
            hud.drawRect(r.x, r.y, r.w, r.h, 0.12f, 0.12f, 0.12f, 0.75f);
            drawItemStack(hud, recipes[i], r.x + 3.0f, r.y + 3.0f, 24.0f, 0.9f);
        }
    }
    if (!view.cursorStack.empty()) { // stack riding the mouse
        drawItemStack(hud, view.cursorStack, view.mouseX - 20, view.mouseY - 20, 40);
    }
}

} // namespace ui
