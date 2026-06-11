#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <GL/glext.h>

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

#include "Audio.h"
#include "Block.h"
#include "Chunk.h"
#include "DayCycle.h"
#include "Entity.h"
#include "Frustum.h"
#include "Hud.h"
#include "Inventory.h"
#include "ItemRenderer.h"
#include "Player.h"
#include "PlayerSave.h"
#include "SaveIO.h"
#include "Settings.h"
#include "Shader.h"
#include "Texture.h"
#include "World.h"

namespace {

constexpr float REACH = 5.0f;           // block interaction distance
constexpr float AUTOSAVE_SECONDS = 30.0f; // periodic world+player save
constexpr float UPLOAD_BUDGET_MS = 3.0f;  // main-thread mesh-upload cap/frame
constexpr float TICK_DT = 0.05f;          // 20 TPS simulation tick
constexpr int MAX_TICKS_PER_FRAME = 5;    // stall guard: drop time, don't spiral
constexpr uint32_t WORLD_SEED = 1337;
const char* SAVE_DIR = "saves/world1";

const Block HOTBAR[] = {Block::Grass, Block::Dirt, Block::Stone,
                        Block::Wood, Block::Leaves, Block::Sand, Block::Torch,
                        Block::Water};
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

enum class Menu { None, Main, Settings }; // pause-menu state

struct App {
    GLFWwindow* window = nullptr;
    Player player;
    PlayerInput input;
    int hotbarSlot = 0;
    Inventory inv;          // survival-mode item storage (row 0 = hotbar)
    ItemStack cursorStack;  // stack carried by the mouse in the inventory UI
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
};

App app;

Block heldBlock() {
    if (!app.survival) return HOTBAR[app.hotbarSlot];
    const ItemStack& s = app.inv.slots[app.hotbarSlot];
    return s.empty() ? Block::Air : s.block;
}

// ---- Inventory UI (survival): rows 1..3 on top, hotbar row 0 below a gap ----

struct InvLayout { float slot = 56.0f, pad = 4.0f; float x0 = 0, y0 = 0; };

InvLayout invLayout(int w, int h) {
    InvLayout L;
    float gw = Inventory::COLS * L.slot + (Inventory::COLS - 1) * L.pad;
    float gh = Inventory::ROWS * L.slot + (Inventory::ROWS - 1) * L.pad + 14.0f;
    L.x0 = (w - gw) * 0.5f;
    L.y0 = (h - gh) * 0.5f;
    return L;
}

float invSlotY(const InvLayout& L, int row) { // row 0 = hotbar, drawn last
    if (row == 0) return L.y0 + 3 * (L.slot + L.pad) + 14.0f;
    return L.y0 + (row - 1) * (L.slot + L.pad);
}

int invSlotAt(int w, int h, float mx, float my) {
    InvLayout L = invLayout(w, h);
    for (int i = 0; i < Inventory::SLOTS; ++i) {
        float x = L.x0 + (i % Inventory::COLS) * (L.slot + L.pad);
        float y = invSlotY(L, i / Inventory::COLS);
        if (mx >= x && mx < x + L.slot && my >= y && my < y + L.slot) return i;
    }
    return -1;
}

// Left click: pick up / put down / swap; same block merges into the slot.
void invClick(int slot) {
    if (slot < 0) return;
    ItemStack& s = app.inv.slots[slot];
    ItemStack& c = app.cursorStack;
    if (c.empty()) {
        c = s;
        s = {};
    } else if (s.empty() || s.block != c.block) {
        std::swap(s, c);
    } else {
        int space = Inventory::STACK_MAX - int(s.count);
        int moved = std::min(space, int(c.count));
        s.count = uint8_t(s.count + moved);
        c.count = uint8_t(c.count - moved);
        if (c.count == 0) c = {};
    }
}

void closeInventory(GLFWwindow* w) {
    app.invOpen = false;
    if (!app.cursorStack.empty()) { // never destroy items on close
        int leftover = app.inv.add(app.cursorStack.block, app.cursorStack.count);
        if (leftover > 0)
            app.entities.spawnItem(app.player.eyePos(), app.player.lookDir() * 3.0f,
                                   app.cursorStack.block, leftover);
        app.cursorStack = {};
    }
    app.mouseCaptured = true;
    app.firstMouse = true;
    glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

// ---- Pause menu: Esc opens it, gameplay freezes behind a dim overlay.
// Every settings change applies live and is written back to settings.cfg.

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float mx, float my) const {
        return mx >= x && mx < x + w && my >= y && my < y + h;
    }
};

const char* const MENU_BUTTONS[] = {"Resume", "Settings", "Quit"};
constexpr int MENU_BUTTON_COUNT = 3;
const char* const SETTING_LABELS[] = {
    "Render distance", "FOV", "Mouse sensitivity", "Volume", "VSync", "Max FPS"};
constexpr int SETTING_COUNT = 6;

// Max FPS choices (the cap applies when vsync is off): common rates up to the
// monitor's refresh, the refresh itself, then 0 = unlimited. Filled in main()
// once the monitor is known.
std::vector<int> fpsOptions = {0};

Rect menuButtonRect(int w, int h, int i) {
    const float bw = 260, bh = 44, gap = 14;
    float y0 = h * 0.5f - (MENU_BUTTON_COUNT * (bh + gap) - gap) * 0.5f;
    return {(w - bw) * 0.5f, y0 + i * (bh + gap), bw, bh};
}

// Settings rows: label left, [-] value [+] right; a Back button sits in the
// extra row slot below.
Rect settingsRowRect(int w, int h, int i) {
    const float rw = 460, rh = 40, gap = 10;
    float y0 = h * 0.5f - ((SETTING_COUNT + 1) * (rh + gap) - gap) * 0.5f;
    return {(w - rw) * 0.5f, y0 + i * (rh + gap), rw, rh};
}
Rect settingsDecRect(const Rect& row) { return {row.x + row.w - 160, row.y + 4, 32, row.h - 8}; }
Rect settingsIncRect(const Rect& row) { return {row.x + row.w - 36, row.y + 4, 32, row.h - 8}; }
Rect settingsBackRect(int w, int h) {
    Rect slot = settingsRowRect(w, h, SETTING_COUNT);
    return {(w - 260) * 0.5f, slot.y, 260.0f, 44.0f};
}

void settingValueText(int row, char* buf, size_t n) {
    const Settings& s = app.settings;
    switch (row) {
        case 0: std::snprintf(buf, n, "%d", s.renderDistance); break;
        case 1: std::snprintf(buf, n, "%.0f", s.fov); break;
        case 2: std::snprintf(buf, n, "%.2f", s.mouseSensitivity); break;
        case 3: std::snprintf(buf, n, "%d%%", int(s.volume * 100.0f + 0.5f)); break;
        case 4: std::snprintf(buf, n, "%s", s.vsync ? "on" : "off"); break;
        case 5:
            if (s.fpsMax == 0) std::snprintf(buf, n, "unlimited");
            else std::snprintf(buf, n, "%d", s.fpsMax);
            break;
    }
}

void adjustSetting(int row, int dir) {
    Settings& s = app.settings;
    switch (row) {
        case 0: s.renderDistance = std::min(16, std::max(2, s.renderDistance + dir)); break;
        case 1: s.fov = std::min(110.0f, std::max(30.0f, s.fov + 5.0f * dir)); break;
        case 2:
            s.mouseSensitivity = std::min(0.5f, std::max(0.02f, s.mouseSensitivity + 0.02f * dir));
            app.player.sensitivity = s.mouseSensitivity;
            break;
        case 3:
            s.volume = std::min(1.0f, std::max(0.0f, s.volume + 0.1f * dir));
            app.audio.setVolume(s.volume);
            break;
        case 4: s.vsync = !s.vsync; glfwSwapInterval(s.vsync ? 1 : 0); break;
        case 5: {
            // Step through the monitor-derived choices. The cfg may hold any
            // value, so first find where it sits in the list (unlimited = 0
            // lives at the end and counts as the highest).
            auto rank = [](int v) { return v == 0 ? 1 << 30 : v; };
            int idx = 0;
            while (idx + 1 < int(fpsOptions.size()) &&
                   rank(fpsOptions[idx]) < rank(s.fpsMax))
                ++idx;
            idx = std::min(int(fpsOptions.size()) - 1, std::max(0, idx + dir));
            s.fpsMax = fpsOptions[idx];
            break;
        }
    }
    s.save("settings.cfg");
}

void openMenu() {
    app.menu = Menu::Main;
    app.mouseCaptured = false;
    glfwSetInputMode(app.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void closeMenu() {
    app.menu = Menu::None;
    app.mouseCaptured = true;
    app.firstMouse = true;
    glfwSetInputMode(app.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void menuClick(int w, int h, float mx, float my) {
    if (app.menu == Menu::Main) {
        for (int i = 0; i < MENU_BUTTON_COUNT; ++i) {
            if (!menuButtonRect(w, h, i).contains(mx, my)) continue;
            if (i == 0) closeMenu();
            else if (i == 1) app.menu = Menu::Settings;
            else glfwSetWindowShouldClose(app.window, GLFW_TRUE);
            return;
        }
    } else if (app.menu == Menu::Settings) {
        for (int i = 0; i < SETTING_COUNT; ++i) {
            Rect row = settingsRowRect(w, h, i);
            if (settingsDecRect(row).contains(mx, my)) { adjustSetting(i, -1); return; }
            if (settingsIncRect(row).contains(mx, my)) { adjustSetting(i, +1); return; }
        }
        if (settingsBackRect(w, h).contains(mx, my)) app.menu = Menu::Main;
    }
}

void drawMenuButton(Hud& hud, const Rect& r, const char* label,
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
    float topY = (app.menu == Menu::Main ? menuButtonRect(sw, sh, 0).y
                                         : settingsRowRect(sw, sh, 0).y);
    hud.drawText((sw - tw) * 0.5f, topY - 56.0f, 3.0f, title);
    if (app.menu == Menu::Main) {
        for (int i = 0; i < MENU_BUTTON_COUNT; ++i)
            drawMenuButton(hud, menuButtonRect(sw, sh, i), MENU_BUTTONS[i],
                           float(mx), float(my));
        return;
    }
    for (int i = 0; i < SETTING_COUNT; ++i) {
        Rect row = settingsRowRect(sw, sh, i);
        hud.drawRect(row.x, row.y, row.w, row.h, 0.1f, 0.1f, 0.1f, 0.7f);
        hud.drawText(row.x + 10, row.y + (row.h - Hud::GLYPH * 2.0f) * 0.5f, 2.0f,
                     SETTING_LABELS[i]);
        drawMenuButton(hud, settingsDecRect(row), "-", float(mx), float(my));
        drawMenuButton(hud, settingsIncRect(row), "+", float(mx), float(my));
        char val[16];
        settingValueText(i, val, sizeof(val));
        // Value centered between the - and + buttons.
        float vx0 = settingsDecRect(row).x + 32, vx1 = settingsIncRect(row).x;
        float vw = std::strlen(val) * Hud::GLYPH * 2.0f;
        hud.drawText(vx0 + (vx1 - vx0 - vw) * 0.5f,
                     row.y + (row.h - Hud::GLYPH * 2.0f) * 0.5f, 2.0f, val);
    }
    drawMenuButton(hud, settingsBackRect(sw, sh), "Back", float(mx), float(my));
}

void keyCallback(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    // if/else rather than switch: inventory and fly are rebindable.
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
            app.invOpen = true;
            app.mouseCaptured = false;
            glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    } else if (key == app.settings.keyFly) {
        if (app.menu == Menu::None) app.player.flying = !app.player.flying;
    } else if (app.menu == Menu::None &&
               key >= GLFW_KEY_1 && key < GLFW_KEY_1 + HOTBAR_SLOTS) {
        app.hotbarSlot = key - GLFW_KEY_1;
    }
}

void mouseButtonCallback(GLFWwindow* w, int button, int action, int) {
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
        if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
            double mx, my;
            glfwGetCursorPos(w, &mx, &my);
            int ww, wh;
            glfwGetWindowSize(w, &ww, &wh); // cursor coords are window coords
            invClick(invSlotAt(ww, wh, float(mx), float(my)));
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
        if (button == GLFW_MOUSE_BUTTON_LEFT) app.breakPressed = true;
        if (button == GLFW_MOUSE_BUTTON_RIGHT) app.placePressed = true;
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
    s.pos = app.player.pos;
    s.yaw = app.player.yaw;
    s.pitch = app.player.pitch;
    s.flying = app.player.flying;
    s.hotbarSlot = uint8_t(app.hotbarSlot);
    s.inv = app.inv;
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
    app.player.pos = s.pos;
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
    int pcx = World::floorDiv((int)std::floor(p.pos.x), CHUNK_SIZE);
    int pcz = World::floorDiv((int)std::floor(p.pos.z), CHUNK_SIZE);
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
        p.pos.x, p.pos.y, p.pos.z,
        pcx, pcz, st.drawn, st.loaded, dayFraction(world.dayTime()),
        st.genMs, st.genQueued, st.meshMs, st.meshQueued, st.uploads,
        hit.hit ? blockName(world.getBlock(hit.block.x, hit.block.y, hit.block.z)) : "-",
        lightBuf, p.flying ? "\n[FLY]" : "");
    // Drop shadow then text, for readability over bright sky.
    hud.drawText(11, 11, 2.0f, buf, 0, 0, 0, 0.6f);
    hud.drawText(10, 10, 2.0f, buf);
}

void drawHotbar(Hud& hud, int screenW, int screenH) {
    const float slot = 56.0f, pad = 4.0f, icon = slot - 2 * pad;
    float totalW = HOTBAR_SLOTS * slot + (HOTBAR_SLOTS - 1) * pad;
    float x0 = (screenW - totalW) * 0.5f;
    float y = screenH - slot - 12.0f;
    for (int i = 0; i < HOTBAR_SLOTS; ++i) {
        float x = x0 + i * (slot + pad);
        bool sel = (i == app.hotbarSlot);
        if (sel) hud.drawRect(x - 3, y - 3, slot + 6, slot + 6, 1, 1, 1, 0.9f);
        hud.drawRect(x, y, slot, slot, 0.1f, 0.1f, 0.1f, 0.65f);
        // Icon: the side texture for grass, base tile otherwise. Survival
        // shows the inventory's hotbar row with stack counts instead of the
        // fixed creative palette.
        if (app.survival) {
            const ItemStack& s = app.inv.slots[i];
            if (!s.empty()) {
                hud.drawTile(x + pad, y + pad, icon, tileFor(s.block, 4), sel ? 1.0f : 0.8f);
                { // always show the count, "1" included — it's the ammo gauge
                    char cnt[4];
                    std::snprintf(cnt, sizeof(cnt), "%d", s.count);
                    float cw = std::strlen(cnt) * Hud::GLYPH * 1.5f;
                    hud.drawText(x + slot - cw - 4, y + slot - 16, 1.5f, cnt);
                }
            }
        } else {
            hud.drawTile(x + pad, y + pad, icon, tileFor(HOTBAR[i], 4), sel ? 1.0f : 0.8f);
        }
        char num[2] = {char('1' + i), 0};
        hud.drawText(x + 4, y + 4, 1.0f, num, 1, 1, 1, 0.8f);
    }
    Block held = heldBlock();
    if (!(app.survival && held == Block::Air)) { // empty slot: no label
        const char* name = blockName(held);
        float nameW = std::strlen(name) * Hud::GLYPH * 2.0f;
        hud.drawText((screenW - nameW) * 0.5f, y - 26.0f, 2.0f, name);
    }
}

void drawInventory(Hud& hud, GLFWwindow* w, int screenW, int screenH) {
    hud.drawRect(0, 0, float(screenW), float(screenH), 0, 0, 0, 0.55f);
    InvLayout L = invLayout(screenW, screenH);
    hud.drawText(L.x0, L.y0 - 26.0f, 2.0f, "Inventory");
    for (int i = 0; i < Inventory::SLOTS; ++i) {
        float x = L.x0 + (i % Inventory::COLS) * (L.slot + L.pad);
        float y = invSlotY(L, i / Inventory::COLS);
        hud.drawRect(x, y, L.slot, L.slot, 0.15f, 0.15f, 0.15f, 0.9f);
        const ItemStack& s = app.inv.slots[i];
        if (s.empty()) continue;
        hud.drawTile(x + L.pad, y + L.pad, L.slot - 2 * L.pad, tileFor(s.block, 4));
        { // count always shown, matching the hotbar
            char cnt[4];
            std::snprintf(cnt, sizeof(cnt), "%d", s.count);
            float cw = std::strlen(cnt) * Hud::GLYPH * 1.5f;
            hud.drawText(x + L.slot - cw - 4, y + L.slot - 16, 1.5f, cnt);
        }
    }
    if (!app.cursorStack.empty()) { // stack riding the mouse
        double mx, my;
        glfwGetCursorPos(w, &mx, &my);
        hud.drawTile(float(mx) - 20, float(my) - 20, 40, tileFor(app.cursorStack.block, 4));
        if (app.cursorStack.count > 1) {
            char cnt[4];
            std::snprintf(cnt, sizeof(cnt), "%d", app.cursorStack.count);
            hud.drawText(float(mx) + 6, float(my) + 6, 1.5f, cnt);
        }
    }
}

void drawCrosshair(Hud& hud, int screenW, int screenH) {
    float cx = screenW * 0.5f, cy = screenH * 0.5f;
    const float len = 9.0f, th = 2.0f;
    hud.drawRect(cx - len, cy - th * 0.5f, len * 2, th, 1, 1, 1, 0.85f);
    hud.drawRect(cx - th * 0.5f, cy - len, th, len * 2, 1, 1, 1, 0.85f);
}

} // namespace

int main(int argc, char** argv) {
    // --frames N : run N frames then exit (with a screenshot) for automated testing.
    // --bench N  : run N frames vsync-off, print perf counters, exit (no screenshot)
    //              — the before/after number for rendering optimization work.
    long maxFrames = -1;
    bool bench = false;
    bool demoItems = false; // spawn a few item entities for screenshot checks
    bool demoInv = false;   // survival + stocked inventory, opened, for screenshots
    Menu demoMenu = Menu::None; // pause menu page opened at start, for screenshots
    float startTime = -1.0f;    // --time <0..1>: day fraction override
    for (int i = 1; i < argc; ++i) {
        if (i < argc - 1) {
            if (std::strcmp(argv[i], "--frames") == 0) maxFrames = std::atol(argv[i + 1]);
            if (std::strcmp(argv[i], "--bench") == 0) { maxFrames = std::atol(argv[i + 1]); bench = true; }
            if (std::strcmp(argv[i], "--time") == 0) startTime = float(std::atof(argv[i + 1]));
        }
        if (std::strcmp(argv[i], "--demo-items") == 0) demoItems = true;
        if (std::strcmp(argv[i], "--demo-inv") == 0) demoInv = true;
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
    const int originLoc = chunkShader.loc("uOrigin");
    Hud hud(atlas);
    ItemRenderer itemRenderer;

    GLuint cubeVbo;
    GLuint cubeVao = makeCubeLines(cubeVbo);

    World world(WORLD_SEED, SAVE_DIR);
    if (startTime >= 0.0f) // --time: pin the day clock for screenshots
        world.setDayTime(startTime * DAY_LENGTH);

    // Audio stays silent if disabled at build time or no device opens.
    app.audio.init();
    app.audio.setVolume(settings.volume);

    bool restored = loadPlayer();
    // Pre-load the area around the player so they don't fall through.
    world.waitUntilLoaded(app.player.pos, 2, 10000);
    if (!restored) app.player.spawn(world);
    app.player.ensureNotStuck(world); // saved position may be inside newer terrain

    if (demoItems) { // a small row of drops in front of the viewpoint
        glm::vec3 base = app.player.eyePos() + app.player.lookDir() * 3.0f;
        app.entities.spawnItem(base, glm::vec3(0.0f), Block::Dirt, 1);
        app.entities.spawnItem(base + glm::vec3(1, 0, 0), glm::vec3(0.0f), Block::Stone, 1);
        app.entities.spawnItem(base + glm::vec3(-1, 0, 0), glm::vec3(0.0f), Block::Wood, 1);
    }
    if (demoInv) {
        app.survival = true;
        app.inv.add(Block::Dirt, 80);
        app.inv.add(Block::Stone, 64);
        app.inv.add(Block::Wood, 5);
        app.inv.add(Block::Torch, 3);
        app.invOpen = true;
        app.mouseCaptured = false;
        glfwSetInputMode(app.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    if (demoMenu != Menu::None) {
        openMenu();
        app.menu = demoMenu;
    }

    double lastTime = glfwGetTime();
    float stepDist = 0.0f;    // ground distance walked since the last footstep
    double accumulator = 0.0; // fixed-timestep simulation accumulator
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

    while (!glfwWindowShouldClose(app.window)) {
        double now = glfwGetTime();
        float dt = float(now - lastTime);
        lastTime = now;
        if (dt > 0.05f) dt = 0.05f; // avoid huge physics steps after stalls

        glfwPollEvents();
        pollMovement();

        // --- Update ---
        // Simulation runs at a fixed 20 TPS (multiplayer insurance: ticks
        // are frame-rate independent); rendering interpolates by alpha.
        // Pause menu open: time simply stops accumulating (items freeze
        // mid-bob, no ticks run), while streaming/rendering continue so
        // render-distance changes apply behind the menu.
        const bool paused = app.menu != Menu::None;
        if (!paused) {
            accumulator += dt;
            gameTime += dt;
        }
        int ticksRun = 0;
        while (accumulator >= TICK_DT) {
            if (++ticksRun > MAX_TICKS_PER_FRAME) { accumulator = 0.0; break; }
            app.player.beginTick();
            // Inventory open: keep simulating (gravity), drop movement intent.
            app.player.update(world, app.invOpen ? PlayerInput{} : app.input, TICK_DT);
            if (app.player.onGround && !app.player.flying) {
                glm::vec3 d = app.player.pos - app.player.prevPos;
                stepDist += std::sqrt(d.x * d.x + d.z * d.z);
                if (stepDist > 2.2f) { // roughly one stride
                    stepDist = 0.0f;
                    app.audio.playFootstep();
                }
            }
            app.entities.tick(world, app.player.pos, &app.inv, TICK_DT);
            accumulator -= TICK_DT;
        }
        const float alpha = float(accumulator / TICK_DT);
        world.update(app.player.pos, settings.renderDistance);

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
        world.processMeshing(8, app.player.pos, &frustum, UPLOAD_BUDGET_MS);

        // Periodic autosave so a crash loses at most ~30 s of edits (chunks
        // streaming out and clean exit already save on their own).
        autosaveTimer += dt;
        if (autosaveTimer >= AUTOSAVE_SECONDS) {
            autosaveTimer = 0.0;
            world.saveAllModified();
            savePlayer();
        }

        RaycastHit hit = world.raycast(eye, dir, REACH);

        if (app.breakPressed && hit.hit &&
            isBreakable(world.getBlock(hit.block.x, hit.block.y, hit.block.z))) {
            Block broken = world.getBlock(hit.block.x, hit.block.y, hit.block.z);
            world.setBlock(hit.block.x, hit.block.y, hit.block.z, Block::Air);
            app.audio.playBreak(soundMaterial(broken));
            // Creative destroys outright; survival drops the registry item.
            if (app.survival) app.entities.spawnBlockDrop(hit.block, broken);
        }
        if (app.placePressed && hit.hit) {
            glm::ivec3 p = hit.adjacent;
            Block held = heldBlock();
            if (held != Block::Air &&
                !isSolid(world.getBlock(p.x, p.y, p.z)) && !app.player.intersectsBlock(p)) {
                if (!app.survival || app.inv.consumeOne(app.hotbarSlot)) {
                    world.setBlock(p.x, p.y, p.z, held);
                    app.audio.playPlace(soundMaterial(held));
                }
            }
        }
        app.breakPressed = app.placePressed = false;

        // --- Render world ---
        // Day/night: the sky (and the fog, which fades into it) follows the
        // world's day clock; sunlight is dimmed in the shader via uSunLevel.
        if (!paused) world.setDayTime(world.dayTime() + dt);
        const glm::vec3 sky = skyColorAt(world.dayTime());
        const float sunLevel = sunLevelAt(world.dayTime());

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

        // Item entities: after opaque (normal depth test), before water so
        // submerged drops blend correctly under the surface.
        itemRenderer.draw(world, app.entities, viewProj, alpha, float(gameTime), sunLevel);

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
        if (!paused) drawHotbar(hud, width, height);
        if (app.invOpen) drawInventory(hud, app.window, width, height);
        if (paused) drawPauseMenu(hud, app.window, width, height);
        hud.end();

        glfwSwapBuffers(app.window);

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
    glDeleteVertexArrays(1, &cubeVao);
    glDeleteBuffers(1, &cubeVbo);
    glfwDestroyWindow(app.window);
    glfwTerminate();
    return 0;
}
