#pragma once

#include "sim/Inventory.h"
#include "world/Block.h"
#include "world/BlockEntity.h"
#include "ui/MenuUi.h"

class Hud;

namespace ui {

// Which inventory screen variant is open. Single source of truth for the
// inventory/crafting-table/furnace distinction (main.cpp uses this directly).
enum class ScreenKind { Inventory, CraftingTable, Furnace };

// Read-only snapshot the renderer needs to compose the inventory screen.
// The module never touches main.cpp globals or GL beyond the Hud API.
struct InventoryView {
    const Inventory& inv;
    const ItemStack& cursorStack;
    const CraftingUiState& crafting;
    ScreenKind screen = ScreenKind::Inventory;
    FurnaceState* furnace = nullptr;  // nullable: only set on the furnace screen
    float mouseX = 0.0f;
    float mouseY = 0.0f;
};

// Read-only snapshot for the bottom hotbar overlay.
struct HotbarView {
    int hotbarSlot = 0;
    bool survival = false;
    const Inventory& inv;
    const Block* palette = nullptr;  // creative palette (one block per slot)
    int paletteCount = 0;
    const char* heldName = nullptr;  // label to show above the hotbar (null/"" = none)
};

// Classic-Minecraft panel style primitives. All operate on ui::Rect.
void drawPanel(Hud& hud, const Rect& r);        // raised bevel, light-gray fill
void drawBeveledSlot(Hud& hud, const Rect& r);  // inset bevel, medium-gray center
void drawArrow(Hud& hud, const Rect& r);        // gray arrow pointing right

void drawItemStack(Hud& hud, const ItemStack& s, float x, float y, float size,
                   float brightness = 1.0f);
void drawHotbar(Hud& hud, const HotbarView& view, int screenW, int screenH);
void drawInventoryScreen(Hud& hud, const InventoryView& view, int screenW,
                         int screenH);

} // namespace ui
