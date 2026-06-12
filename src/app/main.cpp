#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "audio/Audio.h"
#include "world/Block.h"
#include "world/Chunk.h"
#include "world/DayCycle.h"
#include "sim/Entity.h"
#include "render/Frustum.h"
#include "render/GLCompat.h"
#include "ui/Hud.h"
#include "ui/InventoryUi.h"
#include "sim/Inventory.h"
#include "sim/Mining.h"
#include "render/BreakOverlay.h"
#include "render/ItemRenderer.h"
#include "ui/MenuUi.h"
#include "sim/Player.h"
#include "sim/PlayerSave.h"
#include "platform/SaveIO.h"
#include "platform/Settings.h"
#include "render/Shader.h"
#include "render/Texture.h"
#include "sim/TickClock.h"
#include "world/World.h"

namespace {

constexpr float REACH = 5.0f;           // block interaction distance
constexpr float AUTOSAVE_SECONDS = 30.0f; // periodic world+player save
constexpr float UPLOAD_BUDGET_MS = 3.0f;  // main-thread mesh-upload cap/frame
constexpr uint32_t WORLD_SEED = 1337;
const char* SAVE_DIR = "saves/world1";

const Block HOTBAR[] = {Block::Grass, Block::Dirt, Block::Stone,
                        Block::Wood, Block::Leaves, Block::Sand, Block::Torch,
                        Block::Water, Block::Planks};
constexpr int HOTBAR_SLOTS = int(sizeof(HOTBAR) / sizeof(HOTBAR[0]));

// Chunk vertices are packed integers (see ChunkVertex): chunk-local position
// and UV in 1/16 units, brightness byte, texture-array layer. The texture
// array (REPEAT wrap) lets greedy-merged faces tile their texture.
const char* CHUNK_VS = R"(
#version 330 core
layout(location = 0) in ivec3 aPos;
layout(location = 1) in ivec2 aUV;
layout(location = 2) in ivec3 aLightLayer; // sun, block light, tex layer
uniform mat4 uViewProj;
uniform vec3 uOrigin;
uniform float uSunLevel; // day/night: scales the sun channel only
out vec2 vUV;
flat out float vLayer;
out float vLight;
out float vDist;
void main() {
    vUV = vec2(aUV) / 16.0;
    vLayer = float(aLightLayer.z);
    vLight = max(float(aLightLayer.x) / 255.0 * uSunLevel,
                 float(aLightLayer.y) / 255.0);
    vec4 p = uViewProj * vec4(uOrigin + vec3(aPos) / 16.0, 1.0);
    vDist = length(p.xyz);
    gl_Position = p;
}
)";

const char* CHUNK_FS = R"(
#version 330 core
in vec2 vUV;
flat in float vLayer;
in float vLight;
in float vDist;
uniform sampler2DArray uAtlas;
uniform vec3 uSky;
uniform float uFogStart;
uniform float uFogEnd;
out vec4 FragColor;
uniform float uAlpha;
void main() {
    vec3 c = texture(uAtlas, vec3(vUV, vLayer)).rgb * vLight;
    float fog = clamp((vDist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
    FragColor = vec4(mix(c, uSky, fog), uAlpha);
}
)";

const char* LINE_VS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); }
)";

const char* LINE_FS = R"(
#version 330 core
uniform vec3 uColor;
out vec4 FragColor;
void main() { FragColor = vec4(uColor, 1.0); }
)";

using Menu = ui::MenuPage; // pause-menu state

struct App {
    GLFWwindow* window = nullptr;
    Player player;
    PlayerInput input;
    int hotbarSlot = 0;
    Inventory inv;          // survival-mode item storage (row 0 = hotbar)
    ItemStack cursorStack;  // stack carried by the mouse in the inventory UI
    ui::CraftingUiState crafting{2};
    ui::ScreenKind invScreen = ui::ScreenKind::Inventory;
    glm::ivec3 openFurnace{0};
    World* world = nullptr;
    Entities entities;
    Settings settings;
    Audio audio;
    Menu menu = Menu::None;
    bool survival = false;
    bool invOpen = false;
    bool showDebug = true; // F3 toggles the top-left debug/perf overlay
    bool mouseCaptured = true;
    double lastMouseX = 0, lastMouseY = 0;
    bool firstMouse = true;
    bool breakPressed = false, placePressed = false;
    bool breakHeld = false;
    double swingStart = -10.0; // wall-clock start of the held-item click swing
    mining::BreakProgressState breakProgress;
};

App app;

float breakOverlayProgress() {
    if (!app.breakProgress.active || app.breakProgress.requiredTicks <= 0) return 0.0f;
    return std::min(0.999f, float(app.breakProgress.ticks) /
                             float(app.breakProgress.requiredTicks));
}

Block heldBlock() {
    if (!app.survival) return HOTBAR[app.hotbarSlot];
    const ItemStack& s = app.inv.slots[app.hotbarSlot];
    return s.empty() ? Block::Air : placeBlockForItem(s.item);
}

void resetBreakProgress() {
    app.breakProgress = {};
}

void returnCraftingGridToInventory() {
    for (ItemStack& stack : app.crafting.grid.cells) {
        if (stack.empty()) continue;
        int leftover = app.inv.addStack(stack);
        if (leftover > 0) {
            app.entities.spawnItem(app.player.eyePos(), app.player.lookDir() * 3.0f,
                                   ItemStack{stack.item, uint8_t(leftover),
                                             stack.durability});
        }
        stack = {};
    }
}

void openInventoryScreen(GLFWwindow* w, ui::ScreenKind screen, glm::ivec3 pos = {}) {
    resetBreakProgress();
    if (app.invOpen) returnCraftingGridToInventory();
    app.invOpen = true;
    app.invScreen = screen;
    app.openFurnace = pos;
    app.crafting = ui::CraftingUiState(screen == ui::ScreenKind::CraftingTable ? 3 : 2);
    app.mouseCaptured = false;
    glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void survivalMiningTick(World& world) {
    if (!app.survival || app.invOpen || app.menu != Menu::None || !app.breakHeld) {
        resetBreakProgress();
        return;
    }

    RaycastHit hit = world.raycast(app.player.eyePos(), app.player.lookDir(), REACH);
    bool validTarget = hit.hit;
    Block targetBlock = validTarget ? world.getBlock(hit.block.x, hit.block.y, hit.block.z)
                                    : Block::Air;
    ItemStack held = app.inv.slots[app.hotbarSlot];
    mining::BreakProgressEvent ev =
        mining::advanceBreakProgress(app.breakProgress, true, validTarget, hit.block,
                                     targetBlock, held);
    if (!ev.removed) return;

    std::vector<ItemStack> contents;
    if (isFurnaceBlock(targetBlock)) contents = world.takeFurnaceContents(hit.block);
    world.setBlock(hit.block.x, hit.block.y, hit.block.z, Block::Air);
    app.audio.playBreak(soundMaterial(targetBlock));

    if (ev.useDurability) {
        mining::applyDurabilityUse(app.inv.slots[app.hotbarSlot],
                                   mining::DurabilityUseReason::Mining);
    }

    ItemStack drop = mining::miningDrop(targetBlock, mining::miningToolForStack(held));
    if (!drop.empty()) app.entities.spawnBlockDrop(hit.block, drop);
    for (ItemStack stack : contents) app.entities.spawnBlockDrop(hit.block, stack);
}

// ---- Inventory UI (survival): rows 1..3 on top, hotbar row 0 below a gap ----

void closeInventory(GLFWwindow* w) {
    resetBreakProgress();
    returnCraftingGridToInventory();
    app.invOpen = false;
    app.invScreen = ui::ScreenKind::Inventory;
    app.crafting = ui::CraftingUiState(2);
    if (!app.cursorStack.empty()) { // never destroy items on close
        int leftover = app.inv.addStack(app.cursorStack);
        if (leftover > 0)
            app.entities.spawnItem(app.player.eyePos(), app.player.lookDir() * 3.0f,
                                   ItemStack{app.cursorStack.item, uint8_t(leftover),
                                             app.cursorStack.durability});
        app.cursorStack = {};
    }
    app.mouseCaptured = true;
    app.firstMouse = true;
    glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

// ---- Pause menu: Esc opens it, gameplay freezes behind a dim overlay.
// Every settings change applies live and is written back to settings.cfg.

// Max FPS choices (the cap applies when vsync is off): common rates up to the
// monitor's refresh, the refresh itself, then 0 = unlimited. Filled in main()
// once the monitor is known.
std::vector<int> fpsOptions = {0};

void openMenu() {
    resetBreakProgress();
    app.menu = Menu::Main;
    app.mouseCaptured = false;
    glfwSetInputMode(app.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void closeMenu() {
    resetBreakProgress();
    app.menu = Menu::None;
    app.mouseCaptured = true;
    app.firstMouse = true;
    glfwSetInputMode(app.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void applySettingEffects(const ui::SettingEffects& effects) {
    if (effects.mouseSensitivityChanged) app.player.sensitivity = app.settings.mouseSensitivity;
    if (effects.volumeChanged) app.audio.setVolume(app.settings.volume);
    if (effects.vsyncChanged) glfwSwapInterval(app.settings.vsync ? 1 : 0);
    if (effects.saveSettings) app.settings.save("settings.cfg");
}

void menuClick(int w, int h, float mx, float my) {
    ui::MenuCommand command = ui::hitTestMenu(app.menu, w, h, mx, my);
    switch (command.action) {
        case ui::MenuAction::Resume:
            closeMenu();
            break;
        case ui::MenuAction::OpenSettings:
            app.menu = Menu::Settings;
            break;
        case ui::MenuAction::Quit:
            glfwSetWindowShouldClose(app.window, GLFW_TRUE);
            break;
        case ui::MenuAction::Back:
            app.menu = Menu::Main;
            break;
        case ui::MenuAction::AdjustSetting:
            applySettingEffects(
                ui::adjustSetting(app.settings, command.setting, command.dir, fpsOptions));
            break;
        case ui::MenuAction::None:
            break;
    }
}

void drawMenuButton(Hud& hud, const ui::Rect& r, const char* label,
                    float mx, float my, float scale = 2.0f) {
    bool hover = r.contains(mx, my);
    hud.drawRect(r.x, r.y, r.w, r.h, 0.15f, 0.15f, 0.15f, hover ? 0.95f : 0.8f);
    float tw = std::strlen(label) * Hud::GLYPH * scale;
    hud.drawText(r.x + (r.w - tw) * 0.5f, r.y + (r.h - Hud::GLYPH * scale) * 0.5f,
                 scale, label);
}

void drawPauseMenu(Hud& hud, GLFWwindow* w, int sw, int sh) {
    double mx, my;
    glfwGetCursorPos(w, &mx, &my);
    hud.drawRect(0, 0, float(sw), float(sh), 0, 0, 0, 0.55f);
    const char* title = app.menu == Menu::Main ? "Paused" : "Settings";
    float tw = std::strlen(title) * Hud::GLYPH * 3.0f;
    float topY = (app.menu == Menu::Main ? ui::menuButtonRect(sw, sh, 0).y
                                         : ui::settingsRowRect(sw, sh, 0).y);
    hud.drawText((sw - tw) * 0.5f, topY - 56.0f, 3.0f, title);
    if (app.menu == Menu::Main) {
        for (int i = 0; i < ui::MENU_BUTTON_COUNT; ++i)
            drawMenuButton(hud, ui::menuButtonRect(sw, sh, i), ui::MENU_BUTTONS[i],
                           float(mx), float(my));
        return;
    }
    for (int i = 0; i < ui::SETTING_COUNT; ++i) {
        ui::SettingId setting = ui::SettingId(i);
        ui::Rect row = ui::settingsRowRect(sw, sh, i);
        hud.drawRect(row.x, row.y, row.w, row.h, 0.1f, 0.1f, 0.1f, 0.7f);
        hud.drawText(row.x + 10, row.y + (row.h - Hud::GLYPH * 2.0f) * 0.5f, 2.0f,
                     ui::settingLabel(setting));
        drawMenuButton(hud, ui::settingsDecRect(row), "-", float(mx), float(my));
        drawMenuButton(hud, ui::settingsIncRect(row), "+", float(mx), float(my));
        std::string val = ui::settingValueText(app.settings, setting);
        // Value centered between the - and + buttons.
        float vx0 = ui::settingsDecRect(row).x + 32, vx1 = ui::settingsIncRect(row).x;
        float vw = val.size() * Hud::GLYPH * 2.0f;
        hud.drawText(vx0 + (vx1 - vx0 - vw) * 0.5f,
                     row.y + (row.h - Hud::GLYPH * 2.0f) * 0.5f, 2.0f, val.c_str());
    }
    drawMenuButton(hud, ui::settingsBackRect(sw, sh), "Back", float(mx), float(my));
}

void keyCallback(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    // if/else rather than switch: inventory, fly, and mode are rebindable.
    if (key == GLFW_KEY_F3) { // fixed, like Esc
        app.showDebug = !app.showDebug;
    } else if (key == GLFW_KEY_ESCAPE) {
        if (app.invOpen) { closeInventory(w); return; }
        if (app.menu == Menu::Settings) { app.menu = Menu::Main; return; }
        if (app.menu == Menu::Main) { closeMenu(); return; }
        openMenu();
    } else if (key == app.settings.keyInventory) {
        if (!app.survival || app.menu != Menu::None) return;
        if (app.invOpen) {
            closeInventory(w);
        } else {
            openInventoryScreen(w, ui::ScreenKind::Inventory);
        }
    } else if (key == app.settings.keyModeToggle) {
        if (app.menu != Menu::None) return;
        if (app.invOpen) closeInventory(w);
        resetBreakProgress();
        app.survival = !app.survival;
        app.settings.survival = app.survival;
        app.settings.save("settings.cfg");
    } else if (key == app.settings.keyFly) {
        if (app.menu == Menu::None) app.player.flying = !app.player.flying;
    } else if (app.menu == Menu::None &&
               key >= GLFW_KEY_1 && key < GLFW_KEY_1 + HOTBAR_SLOTS) {
        app.hotbarSlot = key - GLFW_KEY_1;
    }
}

ui::UiSlot uiSlotAt(int w, int h, float mx, float my) {
    ui::InventoryLayout L = ui::inventoryLayout(w, h);
    ui::InventorySurface surface = app.invScreen == ui::ScreenKind::Furnace
                                 ? ui::InventorySurface::Furnace
                                 : ui::InventorySurface::Crafting;
    return ui::uiSlotAt(L, surface, app.crafting.grid.width, mx, my);
}

FurnaceState* openFurnaceState() {
    if (!app.world || app.invScreen != ui::ScreenKind::Furnace) return nullptr;
    return app.world->furnaceAt(app.openFurnace);
}

bool canCursorUseFurnaceSlot(const ItemStack& cursor, ui::FurnaceSlot slot) {
    if (cursor.empty()) return true;
    if (slot == ui::FurnaceSlot::Input) return isFurnaceSmeltableInput(cursor.item);
    if (slot == ui::FurnaceSlot::Fuel) return itemDef(cursor.item).fuelTicks > 0;
    return false;
}

void shiftCraftOutputToInventory() {
    while (ui::quickMoveCraftingOutput(app.crafting, app.inv)) {}
}

void handleInventoryUiClick(ui::UiSlot slot, ui::ClickButton btn, bool shift) {
    if (slot.kind == ui::UiSlot::Kind::None) return;
    if (shift) {
        if (slot.kind == ui::UiSlot::Kind::Inventory) {
            if (FurnaceState* f = openFurnaceState())
                ui::quickMoveInventoryToFurnace(app.inv, slot.index, *f);
        } else if (slot.kind == ui::UiSlot::Kind::Craft) {
            ui::quickMoveFromCraftingGrid(app.crafting.grid, slot.index, app.inv);
        } else if (slot.kind == ui::UiSlot::Kind::CraftOutput) {
            shiftCraftOutputToInventory();
        } else if (slot.kind == ui::UiSlot::Kind::Furnace) {
            if (FurnaceState* f = openFurnaceState())
                ui::quickMoveFromFurnace(*f, ui::FurnaceSlot(slot.index), app.inv);
        }
        return;
    }

    if (slot.kind == ui::UiSlot::Kind::Inventory) {
        ui::clickInventorySlot(app.inv, app.cursorStack, slot.index, btn);
    } else if (slot.kind == ui::UiSlot::Kind::Craft) {
        int x = slot.index % app.crafting.grid.width;
        int y = slot.index / app.crafting.grid.width;
        ui::clickStack(app.crafting.grid.at(x, y), app.cursorStack, btn);
    } else if (slot.kind == ui::UiSlot::Kind::CraftOutput) {
        ui::clickCraftingOutput(app.crafting, app.cursorStack, btn);
    } else if (slot.kind == ui::UiSlot::Kind::Furnace) {
        FurnaceState* f = openFurnaceState();
        if (!f) return;
        ui::FurnaceSlot fs = ui::FurnaceSlot(slot.index);
        if (fs == ui::FurnaceSlot::Output) {
            ItemStack& out = ui::furnaceSlotRef(*f, fs);
            if (out.empty()) return;
            if (!app.cursorStack.empty() && !stacksCompatible(out, app.cursorStack)) return;
            ui::clickStack(out, app.cursorStack, btn);
            return;
        }
        if (!canCursorUseFurnaceSlot(app.cursorStack, fs)) return;
        ui::clickStack(ui::furnaceSlotRef(*f, fs), app.cursorStack, btn);
    }
}

void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
        app.breakHeld = false;
    }
    if (app.menu != Menu::None) { // clicks operate the pause menu
        if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
            double mx, my;
            glfwGetCursorPos(w, &mx, &my);
            int ww, wh;
            glfwGetWindowSize(w, &ww, &wh);
            menuClick(ww, wh, float(mx), float(my));
        }
        return;
    }
    if (app.invOpen) { // clicks move stacks instead of recapturing the mouse
        if (action == GLFW_PRESS &&
            (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT)) {
            double mx, my;
            glfwGetCursorPos(w, &mx, &my);
            int ww, wh;
            glfwGetWindowSize(w, &ww, &wh); // cursor coords are window coords
            ui::ClickButton btn = button == GLFW_MOUSE_BUTTON_LEFT
                                ? ui::ClickButton::Left : ui::ClickButton::Right;
            handleInventoryUiClick(uiSlotAt(ww, wh, float(mx), float(my)), btn,
                                   (mods & GLFW_MOD_SHIFT) != 0);
        }
        return;
    }
    if (!app.mouseCaptured) {
        if (action == GLFW_PRESS) {
            app.mouseCaptured = true;
            app.firstMouse = true;
            glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        return;
    }
    if (action == GLFW_PRESS) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            app.breakPressed = true;
            app.breakHeld = true;
        }
        if (button == GLFW_MOUSE_BUTTON_RIGHT) app.placePressed = true;
        app.swingStart = glfwGetTime(); // one viewmodel swing per click
    }
}

void cursorCallback(GLFWwindow*, double x, double y) {
    if (!app.mouseCaptured) return;
    if (app.firstMouse) {
        app.lastMouseX = x;
        app.lastMouseY = y;
        app.firstMouse = false;
        return;
    }
    app.player.look(float(x - app.lastMouseX), float(y - app.lastMouseY));
    app.lastMouseX = x;
    app.lastMouseY = y;
}

void scrollCallback(GLFWwindow*, double, double dy) {
    if (!app.mouseCaptured) return;
    int d = dy > 0 ? -1 : (dy < 0 ? 1 : 0);
    app.hotbarSlot = (app.hotbarSlot + d + HOTBAR_SLOTS) % HOTBAR_SLOTS;
}

void pollMovement() {
    GLFWwindow* w = app.window;
    const Settings& s = app.settings; // movement keys are rebindable
    app.input.forward = glfwGetKey(w, s.keyForward) == GLFW_PRESS;
    app.input.back    = glfwGetKey(w, s.keyBack) == GLFW_PRESS;
    app.input.left    = glfwGetKey(w, s.keyLeft) == GLFW_PRESS;
    app.input.right   = glfwGetKey(w, s.keyRight) == GLFW_PRESS;
    app.input.jump    = glfwGetKey(w, s.keyJump) == GLFW_PRESS;
    app.input.sneak   = glfwGetKey(w, s.keySneak) == GLFW_PRESS;
    app.input.sprint  = glfwGetKey(w, s.keySprint) == GLFW_PRESS;
}

// ---- Player persistence (versioned, format in PlayerSave.h) ----
std::string playerPath() { return std::string(SAVE_DIR) + "/player.bin"; }

void savePlayer() {
    PlayerState s;
    s.pos = app.player.pos();
    s.yaw = app.player.yaw;
    s.pitch = app.player.pitch;
    s.flying = app.player.flying;
    s.hotbarSlot = uint8_t(app.hotbarSlot);
    s.inv = app.inv;
    if (!ui::addTransientStacksForSave(s.inv, app.cursorStack, app.crafting.grid)) {
        std::fprintf(stderr, "warning: skipped player save; transient inventory stacks do not fit\n");
        return;
    }
    if (!savePlayerFile(playerPath(), s))
        std::fprintf(stderr, "warning: failed to save player data\n");
}

bool loadPlayer() {
    PlayerState s;
    if (!loadPlayerFile(playerPath(), s)) {
        if (std::ifstream(playerPath()))
            std::fprintf(stderr, "warning: bad/old player save, starting at spawn\n");
        return false;
    }
    app.player.pos() = s.pos;
    app.player.prevPos = s.pos;
    app.player.yaw = s.yaw;
    app.player.pitch = s.pitch;
    app.player.flying = s.flying;
    app.hotbarSlot = s.hotbarSlot < HOTBAR_SLOTS ? s.hotbarSlot : 0;
    app.inv = s.inv;
    return true;
}

// Wireframe unit cube (12 edges as line list).
GLuint makeCubeLines(GLuint& vbo) {
    const float e = 0.002f; // expand slightly to avoid z-fighting
    float lo = -e, hi = 1.0f + e;
    float v[] = {
        lo,lo,lo, hi,lo,lo,  hi,lo,lo, hi,lo,hi,  hi,lo,hi, lo,lo,hi,  lo,lo,hi, lo,lo,lo,
        lo,hi,lo, hi,hi,lo,  hi,hi,lo, hi,hi,hi,  hi,hi,hi, lo,hi,hi,  lo,hi,hi, lo,hi,lo,
        lo,lo,lo, lo,hi,lo,  hi,lo,lo, hi,hi,lo,  hi,lo,hi, hi,hi,hi,  lo,lo,hi, lo,hi,hi,
    };
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    return vao;
}

void saveScreenshotPPM(const char* path, int w, int h) {
    std::vector<uint8_t> pixels(size_t(w) * h * 3);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y)
        f.write(reinterpret_cast<char*>(&pixels[size_t(y) * w * 3]), size_t(w) * 3);
    std::printf("screenshot saved: %s\n", path);
}

void drawDebugOverlay(Hud& hud, World& world, double fps, float frameMs,
                      const RaycastHit& hit) {
    const Player& p = app.player;
    int pcx = World::floorDiv((int)std::floor(p.pos().x), CHUNK_SIZE);
    int pcz = World::floorDiv((int)std::floor(p.pos().z), CHUNK_SIZE);
    WorldStats st = world.stats();
    char lightBuf[48] = "";
    if (hit.hit) // light of the air cell the targeted face looks into
        std::snprintf(lightBuf, sizeof(lightBuf), "  sun %d torch %d",
                      world.sunLightAt(hit.adjacent.x, hit.adjacent.y, hit.adjacent.z),
                      world.blockLightAt(hit.adjacent.x, hit.adjacent.y, hit.adjacent.z));
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "%.0f fps (%.1f ms)\n"
        "pos %.1f %.1f %.1f\n"
        "chunk %d,%d  drawn %d/%d  day %.2f\n"
        "gen %.1fms q%d  mesh %.1fms q%d up%d\n"
        "target: %s%s%s",
        fps, frameMs,
        p.pos().x, p.pos().y, p.pos().z,
        pcx, pcz, st.drawn, st.loaded, dayFraction(world.dayTime()),
        st.genMs, st.genQueued, st.meshMs, st.meshQueued, st.uploads,
        hit.hit ? blockName(world.getBlock(hit.block.x, hit.block.y, hit.block.z)) : "-",
        lightBuf, p.flying ? "\n[FLY]" : "");
    // Drop shadow then text, for readability over bright sky.
    hud.drawText(11, 11, 2.0f, buf, 0, 0, 0, 0.6f);
    hud.drawText(10, 10, 2.0f, buf);
}

void drawCrosshair(Hud& hud, int screenW, int screenH) {
    float cx = screenW * 0.5f, cy = screenH * 0.5f;
    const float len = 9.0f, th = 2.0f;
    hud.drawRect(cx - len, cy - th * 0.5f, len * 2, th, 1, 1, 1, 0.85f);
    hud.drawRect(cx - th * 0.5f, cy - len, th, len * 2, 1, 1, 1, 0.85f);
}

// Frame-time statistics for --bench-secs: sorted percentiles plus the
// "N% low" fps metric (average fps over the worst N% of frames) that
// benchmarking tools report — it exposes stutter that the average hides.
void printFrameStats(std::vector<float>& ft, double secs) {
    std::sort(ft.begin(), ft.end());
    const size_t n = ft.size();
    if (n == 0) return;
    double sum = 0.0;
    for (float f : ft) sum += f;
    auto pctMs = [&](double p) {
        return double(ft[std::min(n - 1, size_t(p * double(n)))]) * 1000.0;
    };
    auto lowFps = [&](double frac) { // avg fps across the worst frac of frames
        size_t k = std::max<size_t>(1, size_t(frac * double(n)));
        double s = 0.0;
        for (size_t i = n - k; i < n; ++i) s += ft[i];
        return double(k) / s;
    };
    std::printf(
        "frames: %zu in %.2f s\n"
        "fps: avg %.1f | 1%% low %.1f | 0.1%% low %.1f | worst frame %.1f\n"
        "frame ms: min %.2f | p50 %.2f | p95 %.2f | p99 %.2f | max %.2f\n",
        n, secs, double(n) / sum, lowFps(0.01), lowFps(0.001),
        1.0 / double(ft[n - 1]), double(ft[0]) * 1000.0, pctMs(0.50),
        pctMs(0.95), pctMs(0.99), double(ft[n - 1]) * 1000.0);
}

} // namespace

int main(int argc, char** argv) {
    // --frames N : run N frames then exit (with a screenshot) for automated testing.
    // --bench N  : run N frames vsync-off, print perf counters, exit (no screenshot)
    //              — the before/after number for rendering optimization work.
    // --bench-secs S : warm up until chunk streaming settles, then measure S
    //              seconds and print frame-time statistics (avg/1% low/0.1%
    //              low fps, percentiles) — the steady-state stutter check.
    long maxFrames = -1;
    bool bench = false;
    double benchSecs = 0.0;
    bool demoItems = false; // spawn a few item entities for screenshot checks
    bool demoInv = false;   // survival + stocked inventory, opened, for screenshots
    bool demoBreak = false; // survival mining crack overlay for screenshots
    bool demoSurvival = false; // survival loop hotbar + break feedback
    Menu demoMenu = Menu::None; // pause menu page opened at start, for screenshots
    float startTime = -1.0f;    // --time <0..1>: day fraction override
    for (int i = 1; i < argc; ++i) {
        if (i < argc - 1) {
            if (std::strcmp(argv[i], "--frames") == 0) maxFrames = std::atol(argv[i + 1]);
            if (std::strcmp(argv[i], "--bench") == 0) { maxFrames = std::atol(argv[i + 1]); bench = true; }
            if (std::strcmp(argv[i], "--bench-secs") == 0) { benchSecs = std::atof(argv[i + 1]); bench = true; }
            if (std::strcmp(argv[i], "--time") == 0) startTime = float(std::atof(argv[i + 1]));
        }
        if (std::strcmp(argv[i], "--demo-items") == 0) demoItems = true;
        if (std::strcmp(argv[i], "--demo-inv") == 0) demoInv = true;
        if (std::strcmp(argv[i], "--demo-break") == 0) demoBreak = true;
        if (std::strcmp(argv[i], "--demo-survival") == 0) demoSurvival = true;
        if (std::strcmp(argv[i], "--demo-menu") == 0) demoMenu = Menu::Main;
        if (std::strcmp(argv[i], "--demo-settings") == 0) demoMenu = Menu::Settings;
    }

    app.settings = Settings::load("settings.cfg");
    Settings& settings = app.settings; // pause menu edits it live
    app.player.sensitivity = settings.mouseSensitivity;
    app.survival = settings.survival;

    if (!glfwInit()) {
        std::fprintf(stderr, "failed to init GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    int width = 1280, height = 720;
    app.window = glfwCreateWindow(width, height, "Groundwork", nullptr, nullptr);
    if (!app.window) {
        std::fprintf(stderr, "failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(app.window);
    if (!glcompat::load()) {
        std::fprintf(stderr, "failed to load OpenGL function: %s\n",
                     glcompat::missingFunction());
        glfwDestroyWindow(app.window);
        glfwTerminate();
        return 1;
    }
    // Say which device the GL context landed on: on multi-GPU systems (or
    // with a broken driver) this is the difference between the real GPU,
    // an integrated one, and llvmpipe software rendering.
    std::printf("renderer: %s (%s)\n", glGetString(GL_RENDERER), glGetString(GL_VENDOR));
    // A benchmark must not be capped by the display's refresh rate.
    glfwSwapInterval(settings.vsync && !bench ? 1 : 0);

    // Build the Max FPS menu choices around what the monitor supports.
    const GLFWvidmode* vidmode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    const int refresh = (vidmode && vidmode->refreshRate > 0) ? vidmode->refreshRate : 60;
    fpsOptions.clear();
    for (int r : {30, 60, 75, 90, 120, 144, 165, 240, 360})
        if (r < refresh) fpsOptions.push_back(r);
    fpsOptions.push_back(refresh);
    fpsOptions.push_back(0); // unlimited

    glfwSetKeyCallback(app.window, keyCallback);
    glfwSetMouseButtonCallback(app.window, mouseButtonCallback);
    glfwSetCursorPosCallback(app.window, cursorCallback);
    glfwSetScrollCallback(app.window, scrollCallback);
    glfwSetInputMode(app.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(app.window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    Shader chunkShader(CHUNK_VS, CHUNK_FS);
    Shader lineShader(LINE_VS, LINE_FS);
    GLuint atlas = createBlockAtlas();          // 2D strip for the HUD
    GLuint blockTextures = createBlockTextureArray(); // array for chunks
    GLuint crackTextures = createBreakTextureArray(); // RGBA mining cracks
    const int originLoc = chunkShader.loc("uOrigin");
    Hud hud(atlas);
    ItemRenderer itemRenderer;
    BreakOverlay breakOverlay(crackTextures);

    GLuint cubeVbo;
    GLuint cubeVao = makeCubeLines(cubeVbo);

    World world(WORLD_SEED, SAVE_DIR);
    app.world = &world;
    if (startTime >= 0.0f) // --time: pin the day clock for screenshots
        world.setDayTime(startTime * DAY_LENGTH);

    // Audio stays silent if disabled at build time or no device opens.
    app.audio.init();
    app.audio.setVolume(settings.volume);

    bool restored = loadPlayer();
    // Pre-load the area around the player so they don't fall through.
    world.waitUntilLoaded(app.player.pos(), 2, 10000);
    if (!restored) app.player.spawn(world);
    app.player.ensureNotStuck(world); // saved position may be inside newer terrain

    if (demoItems) { // a small row of drops in front of the viewpoint
        glm::vec3 base = app.player.eyePos() + app.player.lookDir() * 3.0f;
        app.entities.spawnItem(base, glm::vec3(0.0f), ItemId::DirtBlock, 1);
        app.entities.spawnItem(base + glm::vec3(1, 0, 0), glm::vec3(0.0f), ItemId::StoneBlock, 1);
        app.entities.spawnItem(base + glm::vec3(-1, 0, 0), glm::vec3(0.0f), ItemId::LogBlock, 1);
        app.entities.spawnItem(base + glm::vec3(2, 0, 0), glm::vec3(0.0f), ItemId::Coal, 1);
        ItemStack pick = makeToolStack(ItemId::IronPickaxe);
        pick.durability = 42;
        app.entities.spawnItem(base + glm::vec3(-2, 0, 0), glm::vec3(0.0f), pick);
    }
    if (demoInv) {
        app.survival = true;
        app.inv.add(ItemId::DirtBlock, 80);
        app.inv.add(ItemId::StoneBlock, 64);
        app.inv.add(ItemId::LogBlock, 5);
        app.inv.add(ItemId::TorchBlock, 3);
        app.invOpen = true;
        app.mouseCaptured = false;
        glfwSetInputMode(app.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    if (demoBreak || demoSurvival) {
        app.survival = true;
        app.player.flying = true;
        app.player.pitch = 0.0f;
        glm::vec3 targetPos = app.player.eyePos() + app.player.lookDir() * 3.0f;
        glm::ivec3 target((int)std::floor(targetPos.x), (int)std::floor(targetPos.y),
                          (int)std::floor(targetPos.z));
        world.setBlock(target.x, target.y, target.z, Block::DiamondOre);
        if (demoSurvival) {
            world.setBlock(target.x - 2, target.y, target.z, Block::CraftingTable);
            world.setBlock(target.x + 2, target.y, target.z,
                           furnaceFacing(app.player.pos().x - (float(target.x + 2) + 0.5f),
                                         app.player.pos().z - (float(target.z) + 0.5f)));
            // Stage the furnace burning so the lit front/glow is visible.
            world.getOrCreateFurnace({target.x + 2, target.y, target.z})
                .burnTicksRemaining = 1 << 20;
            app.inv.slots[0] = makeItemStack(ItemId::LogBlock, 16);
            app.inv.slots[1] = makeItemStack(ItemId::PlanksBlock, 32);
            app.inv.slots[2] = makeToolStack(ItemId::StonePickaxe);
            app.inv.slots[3] = makeItemStack(ItemId::Coal, 8);
            app.inv.slots[4] = makeItemStack(ItemId::TorchBlock, 16);
            app.inv.slots[5] = makeItemStack(ItemId::RawIron, 3);
            app.inv.slots[6] = makeItemStack(ItemId::IronIngot, 2);
            app.inv.slots[7] = makeToolStack(ItemId::IronPickaxe);
            app.hotbarSlot = 7;
        }
        app.breakHeld = false;
        app.breakProgress.active = true;
        app.breakProgress.target = target;
        app.breakProgress.block = Block::DiamondOre;
        app.breakProgress.heldItem = ItemId::None;
        app.breakProgress.ticks = 55;
        app.breakProgress.requiredTicks = 100;
    }

    if (demoMenu != Menu::None) {
        openMenu();
        app.menu = demoMenu;
    }

    double lastTime = glfwGetTime();
    float stepDist = 0.0f;    // ground distance walked since the last footstep
    TickClock tickClock;      // fixed-timestep simulation accumulator
    double gameTime = 0.0;    // sim-side clock (drives item bob/spin)
    double autosaveTimer = 0.0;
    double fpsTimer = 0.0;
    int fpsFrames = 0;
    double fps = 0.0;
    float frameMs = 0.0f;
    long frameCount = 0;
    // Bench accumulators.
    double benchStart = glfwGetTime();
    long benchDrawn = 0, benchUploads = 0;
    // --bench-secs: settle streaming first so startup chunk loading doesn't
    // pollute the steady-state numbers, then time frames swap-to-swap.
    bool benchWarmup = benchSecs > 0.0;
    double benchPrevSwap = -1.0;
    std::vector<float> benchFrames;
    if (benchSecs > 0.0) benchFrames.reserve(size_t(benchSecs * 2000.0));
    // Per-section frame breakdown (only accumulated while measuring): shows
    // where a frame's CPU time goes. "swap" also absorbs GPU sync time.
    constexpr int BENCH_SECTIONS = 9;
    const char* const benchSecName[BENCH_SECTIONS] = {
        "events", "tick", "stream", "mesh", "edit",
        "opaque", "items", "water", "hud+swap"};
    double benchSec[BENCH_SECTIONS] = {};
    double benchSecMark = 0.0;
    auto benchMark = [&](int i) {
        if (!bench || benchWarmup) return;
        double t = glfwGetTime();
        benchSec[i] += t - benchSecMark;
        benchSecMark = t;
    };

    while (!glfwWindowShouldClose(app.window)) {
        double now = glfwGetTime();
        float frameDt = float(now - lastTime);
        lastTime = now;
        float dt = frameDt;
        if (dt > 0.05f) dt = 0.05f; // avoid huge physics steps after stalls

        benchSecMark = now;
        glfwPollEvents();
        pollMovement();
        benchMark(0);

        // --- Update ---
        // Simulation runs at a fixed 20 TPS (multiplayer insurance: ticks
        // are frame-rate independent); rendering interpolates by alpha.
        // Pause menu open: time simply stops accumulating (items freeze
        // mid-bob, no ticks run), while streaming/rendering continue so
        // render-distance changes apply behind the menu.
        const bool paused = app.menu != Menu::None;
        const int ticksToRun = tickClock.advance(frameDt, paused);
        const double simDt = tickClock.consumedSeconds();
        gameTime += simDt;
        if (paused || app.invOpen || !app.survival || !app.breakHeld) resetBreakProgress();
        for (int i = 0; i < ticksToRun; ++i) {
            app.player.beginTick();
            // Inventory open: keep simulating (gravity), drop movement intent.
            app.player.update(world, app.invOpen ? PlayerInput{} : app.input, float(TickClock::TICK_DT));
            survivalMiningTick(world);
            world.tickBlockEntities();
            if (app.player.onGround() && !app.player.flying) {
                glm::vec3 d = app.player.pos() - app.player.prevPos;
                stepDist += std::sqrt(d.x * d.x + d.z * d.z);
                if (stepDist > 2.2f) { // roughly one stride
                    stepDist = 0.0f;
                    app.audio.playFootstep();
                }
            }
            app.entities.tick(world, app.player.pos(), &app.inv, float(TickClock::TICK_DT));
        }
        const float alpha = tickClock.alpha();
        benchMark(1);
        world.update(app.player.pos(), settings.renderDistance);
        benchMark(2);

        // Camera for this frame, computed early: mesh uploads prioritize
        // in-frustum chunks, so processMeshing wants the frustum too.
        glfwGetFramebufferSize(app.window, &width, &height);
        glm::vec3 eye = app.player.eyePos(alpha);
        glm::vec3 dir = app.player.lookDir();
        float aspect = height > 0 ? float(width) / float(height) : 1.0f;
        glm::mat4 proj = glm::perspective(glm::radians(settings.fov), aspect, 0.05f, 600.0f);
        glm::mat4 view = glm::lookAt(eye, eye + dir, glm::vec3(0, 1, 0));
        glm::mat4 viewProj = proj * view;
        Frustum frustum = Frustum::fromMatrix(viewProj);

        // Budgeted mesh uploads (visible/near chunks first), so a streaming
        // burst can't stall a frame on GL transfers.
        world.processMeshing(8, app.player.pos(), &frustum, UPLOAD_BUDGET_MS);
        benchMark(3);

        // Periodic autosave so a crash loses at most ~30 s of edits (chunks
        // streaming out and clean exit already save on their own).
        autosaveTimer += frameDt;
        if (autosaveTimer >= AUTOSAVE_SECONDS) {
            autosaveTimer = 0.0;
            world.saveAllModified();
            savePlayer();
        }

        RaycastHit hit = world.raycast(eye, dir, REACH);
        if ((demoBreak || demoSurvival) && hit.hit) {
            app.breakProgress.active = true;
            app.breakProgress.target = hit.block;
            app.breakProgress.block = world.getBlock(hit.block.x, hit.block.y, hit.block.z);
            app.breakProgress.heldItem = ItemId::None;
            app.breakProgress.ticks = 55;
            app.breakProgress.requiredTicks = 100;
        }
        if (app.invOpen && app.invScreen == ui::ScreenKind::Furnace &&
            !isFurnaceBlock(world.getBlock(app.openFurnace.x, app.openFurnace.y, app.openFurnace.z))) {
            closeInventory(app.window);
        }

        if (!app.survival && app.breakPressed && hit.hit &&
            isBreakable(world.getBlock(hit.block.x, hit.block.y, hit.block.z))) {
            Block broken = world.getBlock(hit.block.x, hit.block.y, hit.block.z);
            world.setBlock(hit.block.x, hit.block.y, hit.block.z, Block::Air);
            app.audio.playBreak(soundMaterial(broken));
        }
        if (app.placePressed && hit.hit) {
            if (app.survival && !app.input.sneak) {
                Block target = world.getBlock(hit.block.x, hit.block.y, hit.block.z);
                if (target == Block::CraftingTable) {
                    openInventoryScreen(app.window, ui::ScreenKind::CraftingTable);
                    app.placePressed = false;
                } else if (isFurnaceBlock(target)) {
                    openInventoryScreen(app.window, ui::ScreenKind::Furnace, hit.block);
                    if (!world.furnaceAt(hit.block)) world.getOrCreateFurnace(hit.block);
                    app.placePressed = false;
                }
            }
        }
        if (app.placePressed && hit.hit) {
            glm::ivec3 p = hit.adjacent;
            Block held = heldBlock();
            if (held == Block::Furnace) // front faces whoever placed it
                held = furnaceFacing(app.player.pos().x - (float(p.x) + 0.5f),
                                     app.player.pos().z - (float(p.z) + 0.5f));
            if (held != Block::Air &&
                !isSolid(world.getBlock(p.x, p.y, p.z)) && !app.player.intersectsBlock(p)) {
                if (!app.survival || app.inv.consumeOne(app.hotbarSlot)) {
                    resetBreakProgress();
                    world.setBlock(p.x, p.y, p.z, held);
                    app.audio.playPlace(soundMaterial(held));
                }
            }
        }
        app.breakPressed = app.placePressed = false;
        benchMark(4);

        // --- Render world ---
        // Day/night: the sky (and the fog, which fades into it) follows the
        // world's day clock; sunlight is dimmed in the shader via uSunLevel.
        world.setDayTime(world.dayTime() + float(simDt));
        const float renderDayTime = world.dayTime() + alpha * float(TickClock::TICK_DT);
        const glm::vec3 sky = skyColorAt(renderDayTime);
        const float sunLevel = sunLevelAt(renderDayTime);

        glViewport(0, 0, width, height);
        glClearColor(sky.r, sky.g, sky.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float fogEnd = float(settings.renderDistance * CHUNK_SIZE);
        chunkShader.use();
        chunkShader.setMat4("uViewProj", viewProj);
        chunkShader.setVec3("uSky", sky);
        chunkShader.setFloat("uFogStart", fogEnd * 0.7f);
        chunkShader.setFloat("uFogEnd", fogEnd * 0.98f);
        chunkShader.setInt("uAtlas", 0);
        chunkShader.setFloat("uAlpha", 1.0f);
        chunkShader.setFloat("uSunLevel", sunLevel);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, blockTextures);
        world.drawChunks(frustum, eye, originLoc);
        benchMark(5);

        // Item entities: after opaque (normal depth test), before water so
        // submerged drops blend correctly under the surface.
        const double renderGameTime = gameTime + alpha * TickClock::TICK_DT;
        itemRenderer.draw(world, app.entities, viewProj, eye, alpha,
                          float(renderGameTime), sunLevel);
        benchMark(6);

        // Translucent water pass: after all opaque geometry, blended, with
        // back faces kept so the surface is visible from underwater.
        chunkShader.use();
        chunkShader.setFloat("uAlpha", 0.65f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        world.drawWater(frustum, eye, originLoc);
        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        benchMark(7);

        if (hit.hit && app.survival && !app.invOpen && app.menu == Menu::None &&
            app.breakProgress.active && app.breakProgress.target == hit.block) {
            breakOverlay.draw(viewProj, hit.block, hit.adjacent, breakOverlayProgress());
        }

        // Selected block outline (shrunk to the post for torches)
        if (hit.hit) {
            lineShader.use();
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(hit.block));
            if (world.getBlock(hit.block.x, hit.block.y, hit.block.z) == Block::Torch) {
                model = glm::translate(model, glm::vec3(7.0f / 16.0f, 0.0f, 7.0f / 16.0f));
                model = glm::scale(model, glm::vec3(2.0f / 16.0f, 10.0f / 16.0f, 2.0f / 16.0f));
            }
            lineShader.setMat4("uMVP", viewProj * model);
            lineShader.setVec3("uColor", glm::vec3(0.05f));
            glBindVertexArray(cubeVao);
            glDrawArrays(GL_LINES, 0, 24);
        }

        // First-person held item / empty-hand arm: the last world-space
        // pass (clears depth so it draws over everything).
        if (!paused) {
            double tnow = glfwGetTime();
            float swing = 0.0f;
            if (app.survival && app.breakHeld && !app.invOpen &&
                app.breakProgress.active) {
                swing = float(std::fmod(tnow, 0.4) / 0.4); // continuous mining chop
            } else if (tnow - app.swingStart < 0.25) {
                swing = float((tnow - app.swingStart) / 0.25); // click swing
            }
            ItemStack heldStack =
                app.survival ? app.inv.slots[app.hotbarSlot]
                             : makeItemStack(itemForBlock(heldBlock()), 1);
            glBindTexture(GL_TEXTURE_2D_ARRAY, blockTextures); // crack pass rebinds unit 0
            itemRenderer.drawHeld(world, heldStack, eye,
                                  float(width) / float(height), sunLevel, swing);
        }

        // --- HUD overlay ---
        ++fpsFrames;
        fpsTimer += dt;
        if (fpsTimer >= 0.25) {
            fps = fpsFrames / fpsTimer;
            frameMs = float(fpsTimer / fpsFrames * 1000.0);
            fpsFrames = 0;
            fpsTimer = 0.0;
        }
        hud.begin(width, height);
        if (!app.invOpen && !paused) drawCrosshair(hud, width, height);
        // Gameplay/debug UI is hidden while the pause menu owns the screen
        // (user feedback); F3 also toggles the debug overlay on its own.
        if (app.showDebug && !paused) drawDebugOverlay(hud, world, fps, frameMs, hit);
        if (!paused) {
            // Empty survival slot: no label. Otherwise show the held block name.
            Block held = heldBlock();
            const char* heldName =
                (app.survival && held == Block::Air) ? nullptr : blockName(held);
            ui::HotbarView hv{app.hotbarSlot, app.survival, app.inv,
                              HOTBAR, HOTBAR_SLOTS, heldName};
            ui::drawHotbar(hud, hv, width, height);
        }
        if (app.invOpen) {
            double mx, my;
            glfwGetCursorPos(app.window, &mx, &my);
            ui::InventoryView iv{app.inv,    app.cursorStack, app.crafting,
                                 app.invScreen, openFurnaceState(),
                                 float(mx), float(my)};
            ui::drawInventoryScreen(hud, iv, width, height);
        }
        if (paused) drawPauseMenu(hud, app.window, width, height);
        hud.end();

        glfwSwapBuffers(app.window);
        benchMark(8);

        // Frame limiter: with vsync off, pace frames to fps_max (0 =
        // unlimited; vsync already paces when on). Sleep most of the wait,
        // then spin the last fraction of a millisecond for accuracy —
        // sleep_for alone overshoots by a scheduler quantum.
        if (!bench && !settings.vsync && settings.fpsMax > 0) {
            const double frameEnd = now + 1.0 / settings.fpsMax; // `now` = frame start
            double t = glfwGetTime();
            // The scheduler can oversleep by ~1 ms, so stop sleeping 2 ms
            // early and spin the remainder.
            if (frameEnd - t > 0.002)
                std::this_thread::sleep_for(
                    std::chrono::duration<double>(frameEnd - t - 0.002));
            while (glfwGetTime() < frameEnd) {}
        }

        if (bench) {
            WorldStats st = world.stats();
            benchDrawn += st.drawn;
            benchUploads += st.uploads;
        }
        if (benchSecs > 0.0) {
            const double t = glfwGetTime();
            WorldStats st = world.stats();
            if (benchWarmup) {
                // Settled = every pipeline empty; cap the wait so a bench in
                // a huge-render-distance world still starts eventually.
                bool settled = st.genQueued == 0 && st.meshQueued == 0 &&
                               st.uploadQueued == 0;
                if ((settled && t - benchStart > 2.0) || t - benchStart > 120.0) {
                    benchWarmup = false;
                    std::printf("bench: warmed up in %.1f s (%d chunks loaded), "
                                "measuring %.0f s...\n",
                                t - benchStart, st.loaded, benchSecs);
                    benchStart = t;
                    benchPrevSwap = t;
                    benchDrawn = benchUploads = 0;
                }
            } else {
                benchFrames.push_back(float(t - benchPrevSwap));
                benchPrevSwap = t;
                if (t - benchStart >= benchSecs) {
                    printFrameStats(benchFrames, t - benchStart);
                    std::printf("sections (ms/frame avg):");
                    for (int i = 0; i < BENCH_SECTIONS; ++i)
                        std::printf(" %s %.2f", benchSecName[i],
                                    benchSec[i] * 1000.0 / double(benchFrames.size()));
                    std::printf("\n");
                    std::printf(
                        "chunks: %d loaded, %.1f drawn/frame avg, %ld mesh uploads\n"
                        "workers: gen %.2f ms/chunk, mesh %.2f ms/chunk (moving avg), "
                        "queues gen %d mesh %d upload %d\n",
                        st.loaded, double(benchDrawn) / double(benchFrames.size()),
                        benchUploads, st.genMs, st.meshMs, st.genQueued,
                        st.meshQueued, st.uploadQueued);
                    break;
                }
            }
        }
        ++frameCount;
        if (maxFrames >= 0 && frameCount >= maxFrames) {
            if (bench) {
                double secs = glfwGetTime() - benchStart;
                WorldStats st = world.stats();
                std::printf(
                    "bench: %ld frames in %.2f s = %.1f fps (%.2f ms/frame)\n"
                    "chunks: %d loaded, %.1f drawn/frame avg, %ld mesh uploads total\n"
                    "workers: gen %.2f ms/chunk, mesh %.2f ms/chunk (moving avg), "
                    "queues gen %d mesh %d upload %d\n",
                    frameCount, secs, frameCount / secs, secs * 1000.0 / frameCount,
                    st.loaded, double(benchDrawn) / frameCount, benchUploads,
                    st.genMs, st.meshMs, st.genQueued, st.meshQueued, st.uploadQueued);
            } else {
                saveScreenshotPPM("screenshot.ppm", width, height);
            }
            break;
        }
    }

    world.saveAllModified();
    savePlayer();
    glDeleteTextures(1, &atlas);
    glDeleteTextures(1, &blockTextures);
    glDeleteTextures(1, &crackTextures);
    glDeleteVertexArrays(1, &cubeVao);
    glDeleteBuffers(1, &cubeVbo);
    glfwDestroyWindow(app.window);
    glfwTerminate();
    return 0;
}
