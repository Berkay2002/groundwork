#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Block.h"
#include "Chunk.h"
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
const glm::vec3 SKY_COLOR(0.53f, 0.71f, 0.92f);
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
layout(location = 2) in ivec2 aLightLayer;
uniform mat4 uViewProj;
uniform vec3 uOrigin;
out vec2 vUV;
flat out float vLayer;
out float vLight;
out float vDist;
void main() {
    vUV = vec2(aUV) / 16.0;
    vLayer = float(aLightLayer.y);
    vLight = float(aLightLayer.x) / 255.0;
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

struct App {
    GLFWwindow* window = nullptr;
    Player player;
    PlayerInput input;
    int hotbarSlot = 0;
    Inventory inv;          // survival-mode item storage (row 0 = hotbar)
    ItemStack cursorStack;  // stack carried by the mouse in the inventory UI
    Entities entities;
    bool survival = false;
    bool invOpen = false;
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

void keyCallback(GLFWwindow* w, int key, int, int action, int) {
    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_E:
                if (!app.survival) break;
                if (app.invOpen) {
                    closeInventory(w);
                } else {
                    app.invOpen = true;
                    app.mouseCaptured = false;
                    glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
                break;
            case GLFW_KEY_ESCAPE:
                if (app.invOpen) { closeInventory(w); break; }
                if (app.mouseCaptured) {
                    app.mouseCaptured = false;
                    glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                } else {
                    glfwSetWindowShouldClose(w, GLFW_TRUE);
                }
                break;
            case GLFW_KEY_F: app.player.flying = !app.player.flying; break;
            default:
                if (key >= GLFW_KEY_1 && key < GLFW_KEY_1 + HOTBAR_SLOTS)
                    app.hotbarSlot = key - GLFW_KEY_1;
        }
    }
}

void mouseButtonCallback(GLFWwindow* w, int button, int action, int) {
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
    app.input.forward = glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS;
    app.input.back    = glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS;
    app.input.left    = glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS;
    app.input.right   = glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS;
    app.input.jump    = glfwGetKey(w, GLFW_KEY_SPACE) == GLFW_PRESS;
    app.input.sneak   = glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    app.input.sprint  = glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
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
        "chunk %d,%d  drawn %d/%d\n"
        "gen %.1fms q%d  mesh %.1fms q%d up%d\n"
        "target: %s%s%s",
        fps, frameMs,
        p.pos.x, p.pos.y, p.pos.z,
        pcx, pcz, st.drawn, st.loaded,
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
    for (int i = 1; i < argc; ++i) {
        if (i < argc - 1) {
            if (std::strcmp(argv[i], "--frames") == 0) maxFrames = std::atol(argv[i + 1]);
            if (std::strcmp(argv[i], "--bench") == 0) { maxFrames = std::atol(argv[i + 1]); bench = true; }
        }
        if (std::strcmp(argv[i], "--demo-items") == 0) demoItems = true;
        if (std::strcmp(argv[i], "--demo-inv") == 0) demoInv = true;
    }

    Settings settings = Settings::load("settings.cfg");
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
    app.window = glfwCreateWindow(width, height, "Minecraft Clone", nullptr, nullptr);
    if (!app.window) {
        std::fprintf(stderr, "failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(app.window);
    // A benchmark must not be capped by the display's refresh rate.
    glfwSwapInterval(settings.vsync && !bench ? 1 : 0);

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

    double lastTime = glfwGetTime();
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
        accumulator += dt;
        gameTime += dt;
        int ticksRun = 0;
        while (accumulator >= TICK_DT) {
            if (++ticksRun > MAX_TICKS_PER_FRAME) { accumulator = 0.0; break; }
            app.player.beginTick();
            // Inventory open: keep simulating (gravity), drop movement intent.
            app.player.update(world, app.invOpen ? PlayerInput{} : app.input, TICK_DT);
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
            // Creative destroys outright; survival drops the registry item.
            if (app.survival) app.entities.spawnBlockDrop(hit.block, broken);
        }
        if (app.placePressed && hit.hit) {
            glm::ivec3 p = hit.adjacent;
            Block held = heldBlock();
            if (held != Block::Air &&
                !isSolid(world.getBlock(p.x, p.y, p.z)) && !app.player.intersectsBlock(p)) {
                if (!app.survival || app.inv.consumeOne(app.hotbarSlot))
                    world.setBlock(p.x, p.y, p.z, held);
            }
        }
        app.breakPressed = app.placePressed = false;

        // --- Render world ---
        glViewport(0, 0, width, height);
        glClearColor(SKY_COLOR.r, SKY_COLOR.g, SKY_COLOR.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float fogEnd = float(settings.renderDistance * CHUNK_SIZE);
        chunkShader.use();
        chunkShader.setMat4("uViewProj", viewProj);
        chunkShader.setVec3("uSky", SKY_COLOR);
        chunkShader.setFloat("uFogStart", fogEnd * 0.7f);
        chunkShader.setFloat("uFogEnd", fogEnd * 0.98f);
        chunkShader.setInt("uAtlas", 0);
        chunkShader.setFloat("uAlpha", 1.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, blockTextures);
        world.drawChunks(frustum, eye, originLoc);

        // Item entities: after opaque (normal depth test), before water so
        // submerged drops blend correctly under the surface.
        itemRenderer.draw(world, app.entities, viewProj, alpha, float(gameTime));

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
        if (!app.invOpen) drawCrosshair(hud, width, height);
        drawDebugOverlay(hud, world, fps, frameMs, hit);
        drawHotbar(hud, width, height);
        if (app.invOpen) drawInventory(hud, app.window, width, height);
        hud.end();

        glfwSwapBuffers(app.window);

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
    glfwDestroyWindow(app.window);
    glfwTerminate();
    return 0;
}
