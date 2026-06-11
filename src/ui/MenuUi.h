#pragma once

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "sim/Crafting.h"
#include "sim/Inventory.h"
#include "platform/Settings.h"
#include "world/BlockEntity.h"

namespace ui {

enum class MenuPage { None, Main, Settings };
enum class MenuAction { None, Resume, OpenSettings, Quit, Back, AdjustSetting };
enum class ClickButton { Left, Right };
enum class FurnaceSlot { Input, Fuel, Output };
enum class InventorySurface { Crafting, Furnace };
enum class SettingId {
    RenderDistance = 0,
    Fov,
    MouseSensitivity,
    Volume,
    Vsync,
    FpsMax,
    Count
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float mx, float my) const {
        return mx >= x && mx < x + w && my >= y && my < y + h;
    }
};

struct MenuCommand {
    MenuAction action = MenuAction::None;
    SettingId setting = SettingId::RenderDistance;
    int dir = 0;
};

struct SettingEffects {
    bool saveSettings = false;
    bool mouseSensitivityChanged = false;
    bool volumeChanged = false;
    bool vsyncChanged = false;
};

struct InventoryLayout {
    float slot = 56.0f;
    float pad = 4.0f;
    float x0 = 0;
    float y0 = 0;
};

struct UiSlot {
    enum class Kind { None, Inventory, Craft, CraftOutput, Furnace, RecipeReference };
    Kind kind = Kind::None;
    int index = -1;

    static UiSlot none() { return {}; }
    static UiSlot inventory(int slot) { return {Kind::Inventory, slot}; }
    static UiSlot craft(int slot) { return {Kind::Craft, slot}; }
    static UiSlot craftOutput() { return {Kind::CraftOutput, 0}; }
    static UiSlot furnace(FurnaceSlot slot) { return {Kind::Furnace, int(slot)}; }
    static UiSlot recipeReference(int slot) { return {Kind::RecipeReference, slot}; }
    bool operator==(const UiSlot& o) const { return kind == o.kind && index == o.index; }
    bool operator!=(const UiSlot& o) const { return !(*this == o); }
};

struct CraftingUiState {
    explicit CraftingUiState(int surface = 2) {
        grid.width = surface;
        grid.height = surface;
    }
    crafting::CraftingGrid grid;
};

inline constexpr const char* MENU_BUTTONS[] = {"Resume", "Settings", "Quit"};
inline constexpr int MENU_BUTTON_COUNT = 3;
inline constexpr const char* SETTING_LABELS[] = {
    "Render distance", "FOV", "Mouse sensitivity", "Volume", "VSync", "Max FPS"};
inline constexpr int SETTING_COUNT = int(SettingId::Count);

inline Rect menuButtonRect(int w, int h, int i) {
    const float bw = 260, bh = 44, gap = 14;
    float y0 = h * 0.5f - (MENU_BUTTON_COUNT * (bh + gap) - gap) * 0.5f;
    return {(w - bw) * 0.5f, y0 + i * (bh + gap), bw, bh};
}

inline Rect settingsRowRect(int w, int h, int i) {
    const float rw = 460, rh = 40, gap = 10;
    float y0 = h * 0.5f - ((SETTING_COUNT + 1) * (rh + gap) - gap) * 0.5f;
    return {(w - rw) * 0.5f, y0 + i * (rh + gap), rw, rh};
}

inline Rect settingsDecRect(const Rect& row) {
    return {row.x + row.w - 160, row.y + 4, 32, row.h - 8};
}

inline Rect settingsIncRect(const Rect& row) {
    return {row.x + row.w - 36, row.y + 4, 32, row.h - 8};
}

inline Rect settingsBackRect(int w, int h) {
    Rect slot = settingsRowRect(w, h, SETTING_COUNT);
    return {(w - 260) * 0.5f, slot.y, 260.0f, 44.0f};
}

inline MenuCommand hitTestMenu(MenuPage page, int w, int h, float mx, float my) {
    if (page == MenuPage::Main) {
        for (int i = 0; i < MENU_BUTTON_COUNT; ++i) {
            if (!menuButtonRect(w, h, i).contains(mx, my)) continue;
            if (i == 0) return {MenuAction::Resume, SettingId::RenderDistance, 0};
            if (i == 1) return {MenuAction::OpenSettings, SettingId::RenderDistance, 0};
            return {MenuAction::Quit, SettingId::RenderDistance, 0};
        }
    } else if (page == MenuPage::Settings) {
        for (int i = 0; i < SETTING_COUNT; ++i) {
            Rect row = settingsRowRect(w, h, i);
            if (settingsDecRect(row).contains(mx, my))
                return {MenuAction::AdjustSetting, SettingId(i), -1};
            if (settingsIncRect(row).contains(mx, my))
                return {MenuAction::AdjustSetting, SettingId(i), 1};
        }
        if (settingsBackRect(w, h).contains(mx, my))
            return {MenuAction::Back, SettingId::RenderDistance, 0};
    }
    return {};
}

inline SettingEffects adjustSetting(Settings& s, SettingId setting, int dir,
                                    const std::vector<int>& fpsOptions) {
    SettingEffects effects;
    switch (setting) {
        case SettingId::RenderDistance:
            s.renderDistance = std::min(16, std::max(2, s.renderDistance + dir));
            break;
        case SettingId::Fov:
            s.fov = std::min(110.0f, std::max(30.0f, s.fov + 5.0f * dir));
            break;
        case SettingId::MouseSensitivity:
            s.mouseSensitivity =
                std::min(0.5f, std::max(0.02f, s.mouseSensitivity + 0.02f * dir));
            effects.mouseSensitivityChanged = true;
            break;
        case SettingId::Volume:
            s.volume = std::min(1.0f, std::max(0.0f, s.volume + 0.1f * dir));
            effects.volumeChanged = true;
            break;
        case SettingId::Vsync:
            s.vsync = !s.vsync;
            effects.vsyncChanged = true;
            break;
        case SettingId::FpsMax: {
            if (fpsOptions.empty()) break;
            auto rank = [](int v) { return v == 0 ? 1 << 30 : v; };
            int idx = 0;
            while (idx + 1 < int(fpsOptions.size()) &&
                   rank(fpsOptions[idx]) < rank(s.fpsMax))
                ++idx;
            idx = std::min(int(fpsOptions.size()) - 1, std::max(0, idx + dir));
            s.fpsMax = fpsOptions[idx];
            break;
        }
        case SettingId::Count:
            break;
    }
    effects.saveSettings = true;
    return effects;
}

inline const char* settingLabel(SettingId setting) {
    return SETTING_LABELS[int(setting)];
}

inline std::string settingValueText(const Settings& s, SettingId setting) {
    char buf[32] = "";
    switch (setting) {
        case SettingId::RenderDistance:
            std::snprintf(buf, sizeof(buf), "%d", s.renderDistance);
            break;
        case SettingId::Fov:
            std::snprintf(buf, sizeof(buf), "%.0f", s.fov);
            break;
        case SettingId::MouseSensitivity:
            std::snprintf(buf, sizeof(buf), "%.2f", s.mouseSensitivity);
            break;
        case SettingId::Volume:
            std::snprintf(buf, sizeof(buf), "%d%%", int(s.volume * 100.0f + 0.5f));
            break;
        case SettingId::Vsync:
            std::snprintf(buf, sizeof(buf), "%s", s.vsync ? "on" : "off");
            break;
        case SettingId::FpsMax:
            if (s.fpsMax == 0) std::snprintf(buf, sizeof(buf), "unlimited");
            else std::snprintf(buf, sizeof(buf), "%d", s.fpsMax);
            break;
        case SettingId::Count:
            break;
    }
    return buf;
}

inline InventoryLayout inventoryLayout(int w, int h) {
    InventoryLayout L;
    float gw = Inventory::COLS * L.slot + (Inventory::COLS - 1) * L.pad;
    float gh = Inventory::ROWS * L.slot + (Inventory::ROWS - 1) * L.pad + 14.0f;
    L.x0 = (w - gw) * 0.5f;
    L.y0 = (h - gh) * 0.5f;
    return L;
}

inline float inventorySlotY(const InventoryLayout& L, int row) {
    if (row == 0) return L.y0 + 3 * (L.slot + L.pad) + 14.0f;
    return L.y0 + (row - 1) * (L.slot + L.pad);
}

inline Rect inventorySlotRect(const InventoryLayout& L, int slot) {
    return {L.x0 + (slot % Inventory::COLS) * (L.slot + L.pad),
            inventorySlotY(L, slot / Inventory::COLS),
            L.slot,
            L.slot};
}

inline int inventorySlotAt(int w, int h, float mx, float my) {
    InventoryLayout L = inventoryLayout(w, h);
    for (int i = 0; i < Inventory::SLOTS; ++i)
        if (inventorySlotRect(L, i).contains(mx, my)) return i;
    return -1;
}

inline int inventorySlotAt(const InventoryLayout& L, float mx, float my) {
    for (int i = 0; i < Inventory::SLOTS; ++i)
        if (inventorySlotRect(L, i).contains(mx, my)) return i;
    return -1;
}

inline Rect craftSlotRect(const InventoryLayout& L, int surface, int x, int y) {
    float panelW = surface * L.slot + (surface - 1) * L.pad;
    float x0 = L.x0 - panelW - 86.0f;
    return {x0 + x * (L.slot + L.pad), L.y0 + y * (L.slot + L.pad), L.slot, L.slot};
}

inline Rect craftOutputRect(const InventoryLayout& L, int surface) {
    Rect last = craftSlotRect(L, surface, surface - 1, surface / 2);
    return {last.x + L.slot + 20.0f, last.y, L.slot, L.slot};
}

inline Rect furnaceSlotRect(const InventoryLayout& L, FurnaceSlot slot) {
    float x0 = L.x0 - 210.0f;
    float y0 = L.y0 + 20.0f;
    if (slot == FurnaceSlot::Input) return {x0, y0, L.slot, L.slot};
    if (slot == FurnaceSlot::Fuel) return {x0, y0 + L.slot + 18.0f, L.slot, L.slot};
    return {x0 + L.slot + 56.0f, y0 + (L.slot + 18.0f) * 0.5f, L.slot, L.slot};
}

inline Rect recipeReferenceSlotRect(const InventoryLayout& L, int index) {
    float rx = L.x0 + Inventory::COLS * (L.slot + L.pad) + 30.0f;
    float ry = L.y0;
    return {rx + float(index % 3) * 34.0f, ry + float(index / 3) * 34.0f,
            30.0f, 30.0f};
}

inline int recipeReferenceSlotAt(const InventoryLayout& L, float mx, float my,
                                 int count = int(crafting::recipeCount())) {
    for (int i = 0; i < count; ++i)
        if (recipeReferenceSlotRect(L, i).contains(mx, my)) return i;
    return -1;
}

inline UiSlot uiSlotAt(const InventoryLayout& L, InventorySurface surface,
                       int craftSurface, float mx, float my) {
    int invSlot = inventorySlotAt(L, mx, my);
    if (invSlot >= 0) return UiSlot::inventory(invSlot);
    if (surface == InventorySurface::Furnace) {
        for (FurnaceSlot s : {FurnaceSlot::Input, FurnaceSlot::Fuel, FurnaceSlot::Output})
            if (furnaceSlotRect(L, s).contains(mx, my)) return UiSlot::furnace(s);
        return UiSlot::none();
    }
    for (int y = 0; y < craftSurface; ++y)
        for (int x = 0; x < craftSurface; ++x)
            if (craftSlotRect(L, craftSurface, x, y).contains(mx, my))
                return UiSlot::craft(y * craftSurface + x);
    if (craftOutputRect(L, craftSurface).contains(mx, my)) return UiSlot::craftOutput();
    int recipe = recipeReferenceSlotAt(L, mx, my);
    return recipe >= 0 ? UiSlot::recipeReference(recipe) : UiSlot::none();
}

inline void clickStack(ItemStack& slot, ItemStack& cursor, ClickButton button) {
    if (button == ClickButton::Left) {
        if (cursor.empty()) {
            cursor = slot;
            slot = {};
        } else if (slot.empty() || !stacksCompatible(slot, cursor)) {
            std::swap(slot, cursor);
        } else {
            int space = int(itemDef(slot.item).stackMax) - int(slot.count);
            int moved = std::min(space, int(cursor.count));
            slot.count = uint8_t(slot.count + moved);
            cursor.count = uint8_t(cursor.count - moved);
            if (cursor.count == 0) cursor = {};
        }
        return;
    }

    if (cursor.empty()) {
        if (slot.empty()) return;
        int take = (int(slot.count) + 1) / 2;
        cursor = slot;
        cursor.count = uint8_t(take);
        slot.count = uint8_t(slot.count - take);
        if (slot.count == 0) slot = {};
    } else if (slot.empty()) {
        slot = cursor;
        slot.count = 1;
        if (--cursor.count == 0) cursor = {};
    } else if (stacksCompatible(slot, cursor) && slot.count < itemDef(slot.item).stackMax) {
        ++slot.count;
        if (--cursor.count == 0) cursor = {};
    }
}

inline void clickInventorySlot(Inventory& inv, ItemStack& cursor, int slot,
                               ClickButton button = ClickButton::Left) {
    if (slot < 0 || slot >= Inventory::SLOTS) return;
    clickStack(inv.slots[slot], cursor, button);
}

inline UiSlot craftingSlotAt(const CraftingUiState& state, int x, int y) {
    if (x < 0 || y < 0 || x >= state.grid.width || y >= state.grid.height)
        return UiSlot::none();
    return UiSlot::craft(y * state.grid.width + x);
}

inline UiSlot craftingOutputSlot() { return UiSlot::craftOutput(); }

inline bool clickCraftingOutput(CraftingUiState& state, ItemStack& cursor,
                                ClickButton) {
    return crafting::craftToCursor(state.grid, cursor);
}

inline bool quickMoveCraftingOutput(CraftingUiState& state, Inventory& inv) {
    ItemStack out = crafting::craftingOutput(state.grid);
    if (out.empty()) return false;
    Inventory trial = inv;
    if (trial.addStack(out) != 0) return false;
    ItemStack cursor;
    if (!crafting::craftToCursor(state.grid, cursor)) return false;
    inv = trial;
    return true;
}

inline bool quickMoveFromCraftingGrid(crafting::CraftingGrid& grid, int slot,
                                      Inventory& inv) {
    if (slot < 0 || slot >= grid.width * grid.height) return false;
    int x = slot % grid.width, y = slot / grid.width;
    ItemStack& s = grid.at(x, y);
    if (s.empty()) return false;
    int leftover = inv.addStack(s);
    if (leftover == s.count) return false;
    if (leftover == 0) s = {};
    else s.count = uint8_t(leftover);
    return true;
}

inline bool addTransientStacksForSave(Inventory& inv, const ItemStack& cursor,
                                      const crafting::CraftingGrid& grid) {
    bool allStored = true;
    auto add = [&](const ItemStack& stack) {
        if (stack.empty()) return;
        if (inv.addStack(stack) != 0) allStored = false;
    };
    add(cursor);
    for (const ItemStack& stack : grid.cells) add(stack);
    return allStored;
}

inline ItemStack& furnaceSlotRef(FurnaceState& furnace, FurnaceSlot slot) {
    if (slot == FurnaceSlot::Input) return furnace.input;
    if (slot == FurnaceSlot::Fuel) return furnace.fuel;
    return furnace.output;
}

inline bool quickMoveFromFurnace(FurnaceState& furnace, FurnaceSlot slot,
                                 Inventory& inv) {
    ItemStack& s = furnaceSlotRef(furnace, slot);
    if (s.empty()) return false;
    int leftover = inv.addStack(s);
    if (leftover == s.count) return false;
    if (leftover == 0) s = {};
    else s.count = uint8_t(leftover);
    return true;
}

inline bool quickMoveInventoryToFurnace(Inventory& inv, int invSlot,
                                        FurnaceState& furnace) {
    if (invSlot < 0 || invSlot >= Inventory::SLOTS) return false;
    ItemStack& s = inv.slots[invSlot];
    if (s.empty()) return false;
    FurnaceSlot dest;
    if (itemDef(s.item).fuelTicks > 0) dest = FurnaceSlot::Fuel;
    else if (isFurnaceSmeltableInput(s.item)) dest = FurnaceSlot::Input;
    else return false;
    ItemStack& target = furnaceSlotRef(furnace, dest);
    if (!target.empty() && !stacksCompatible(target, s)) return false;
    int max = itemDef(s.item).stackMax;
    int space = target.empty() ? max : max - int(target.count);
    if (space <= 0) return false;
    int moved = std::min(space, int(s.count));
    if (target.empty()) {
        target = s;
        target.count = uint8_t(moved);
    } else {
        target.count = uint8_t(target.count + moved);
    }
    s.count = uint8_t(s.count - moved);
    if (s.count == 0) s = {};
    return true;
}

inline std::vector<ItemStack> recipeReferenceOutputs() {
    std::vector<ItemStack> out;
    out.reserve(crafting::recipeCount());
    for (size_t i = 0; i < crafting::recipeCount(); ++i)
        out.push_back(crafting::recipeAt(i).output);
    return out;
}

} // namespace ui
