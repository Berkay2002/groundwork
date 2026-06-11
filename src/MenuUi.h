#pragma once

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "Inventory.h"
#include "Settings.h"

namespace ui {

enum class MenuPage { None, Main, Settings };
enum class MenuAction { None, Resume, OpenSettings, Quit, Back, AdjustSetting };
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

inline void clickInventorySlot(Inventory& inv, ItemStack& cursor, int slot) {
    if (slot < 0 || slot >= Inventory::SLOTS) return;
    ItemStack& s = inv.slots[slot];
    if (cursor.empty()) {
        cursor = s;
        s = {};
    } else if (s.empty() || s.block != cursor.block) {
        std::swap(s, cursor);
    } else {
        int space = Inventory::STACK_MAX - int(s.count);
        int moved = std::min(space, int(cursor.count));
        s.count = uint8_t(s.count + moved);
        cursor.count = uint8_t(cursor.count - moved);
        if (cursor.count == 0) cursor = {};
    }
}

} // namespace ui
