#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Block.h"
#include "Chunk.h"
#include "Frustum.h"
#include "Hud.h"
#include "Player.h"
#include "Settings.h"
#include "Shader.h"
#include "Texture.h"
#include "World.h"

namespace {

constexpr float REACH = 5.0f;           // block interaction distance
constexpr uint32_t WORLD_SEED = 1337;
const glm::vec3 SKY_COLOR(0.53f, 0.71f, 0.92f);
const char* SAVE_DIR = "saves/world1";

const Block HOTBAR[] = {Block::Grass, Block::Dirt, Block::Stone,
                        Block::Wood, Block::Leaves, Block::Sand, Block::Torch};
constexpr int HOTBAR_SLOTS = int(sizeof(HOTBAR) / sizeof(HOTBAR[0]));

const char* CHUNK_VS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in float aLight;
uniform mat4 uViewProj;
out vec2 vUV;
out float vLight;
out float vDist;
void main() {
    vUV = aUV;
    vLight = aLight;
    vec4 p = uViewProj * vec4(aPos, 1.0);
    vDist = length(p.xyz);
    gl_Position = p;
}
)";

const char* CHUNK_FS = R"(
#version 330 core
in vec2 vUV;
in float vLight;
in float vDist;
uniform sampler2D uAtlas;
uniform vec3 uSky;
uniform float uFogStart;
uniform float uFogEnd;
out vec4 FragColor;
void main() {
    vec3 c = texture(uAtlas, vUV).rgb * vLight;
    float fog = clamp((vDist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
    FragColor = vec4(mix(c, uSky, fog), 1.0);
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
    bool mouseCaptured = true;
    double lastMouseX = 0, lastMouseY = 0;
    bool firstMouse = true;
    bool breakPressed = false, placePressed = false;
};

App app;

Block heldBlock() { return HOTBAR[app.hotbarSlot]; }

void keyCallback(GLFWwindow* w, int key, int, int action, int) {
    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_ESCAPE:
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

const char* blockName(Block b) {
    switch (b) {
        case Block::Grass:   return "Grass";
        case Block::Dirt:    return "Dirt";
        case Block::Stone:   return "Stone";
        case Block::Wood:    return "Wood";
        case Block::Leaves:  return "Leaves";
        case Block::Sand:    return "Sand";
        case Block::Bedrock: return "Bedrock";
        case Block::Torch:   return "Torch";
        default:             return "Air";
    }
}

// ---- Player persistence (versioned) ----
constexpr char PLAYER_MAGIC[4] = {'M', 'C', 'P', 'L'};
constexpr uint32_t PLAYER_VERSION = 1;

std::string playerPath() { return std::string(SAVE_DIR) + "/player.bin"; }

void savePlayer() {
    std::ofstream f(playerPath(), std::ios::binary);
    if (!f) { std::fprintf(stderr, "warning: failed to save player data\n"); return; }
    f.write(PLAYER_MAGIC, 4);
    f.write(reinterpret_cast<const char*>(&PLAYER_VERSION), 4);
    const Player& p = app.player;
    f.write(reinterpret_cast<const char*>(&p.pos), sizeof(p.pos));
    f.write(reinterpret_cast<const char*>(&p.yaw), sizeof(p.yaw));
    f.write(reinterpret_cast<const char*>(&p.pitch), sizeof(p.pitch));
    uint8_t flying = p.flying ? 1 : 0;
    uint8_t slot = (uint8_t)app.hotbarSlot;
    f.write(reinterpret_cast<const char*>(&flying), 1);
    f.write(reinterpret_cast<const char*>(&slot), 1);
}

bool loadPlayer() {
    std::ifstream f(playerPath(), std::ios::binary);
    if (!f) return false;
    char magic[4];
    uint32_t version = 0;
    f.read(magic, 4);
    f.read(reinterpret_cast<char*>(&version), 4);
    if (!f || std::memcmp(magic, PLAYER_MAGIC, 4) != 0 || version != PLAYER_VERSION) {
        std::fprintf(stderr, "warning: bad/old player save, starting at spawn\n");
        return false;
    }
    Player& p = app.player;
    f.read(reinterpret_cast<char*>(&p.pos), sizeof(p.pos));
    f.read(reinterpret_cast<char*>(&p.yaw), sizeof(p.yaw));
    f.read(reinterpret_cast<char*>(&p.pitch), sizeof(p.pitch));
    uint8_t flying = 0, slot = 0;
    f.read(reinterpret_cast<char*>(&flying), 1);
    f.read(reinterpret_cast<char*>(&slot), 1);
    if (!f) return false;
    p.flying = flying != 0;
    app.hotbarSlot = slot < HOTBAR_SLOTS ? slot : 0;
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
        // Icon: the side texture for grass, base tile otherwise.
        int tile = tileFor(HOTBAR[i], 4);
        hud.drawTile(x + pad, y + pad, icon, tile, sel ? 1.0f : 0.8f);
        char num[2] = {char('1' + i), 0};
        hud.drawText(x + 4, y + 4, 1.0f, num, 1, 1, 1, 0.8f);
    }
    const char* name = blockName(heldBlock());
    float nameW = std::strlen(name) * Hud::GLYPH * 2.0f;
    hud.drawText((screenW - nameW) * 0.5f, y - 26.0f, 2.0f, name);
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
    long maxFrames = -1;
    for (int i = 1; i < argc - 1; ++i)
        if (std::strcmp(argv[i], "--frames") == 0) maxFrames = std::atol(argv[i + 1]);

    Settings settings = Settings::load("settings.cfg");
    app.player.sensitivity = settings.mouseSensitivity;

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
    glfwSwapInterval(settings.vsync ? 1 : 0);

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
    GLuint atlas = createBlockAtlas();
    Hud hud(atlas);

    GLuint cubeVbo;
    GLuint cubeVao = makeCubeLines(cubeVbo);

    World world(WORLD_SEED, SAVE_DIR);

    bool restored = loadPlayer();
    // Pre-load the area around the player so they don't fall through.
    world.waitUntilLoaded(app.player.pos, 2, 10000);
    if (!restored) app.player.spawn(world);
    app.player.ensureNotStuck(world); // saved position may be inside newer terrain

    double lastTime = glfwGetTime();
    double fpsTimer = 0.0;
    int fpsFrames = 0;
    double fps = 0.0;
    float frameMs = 0.0f;
    long frameCount = 0;

    while (!glfwWindowShouldClose(app.window)) {
        double now = glfwGetTime();
        float dt = float(now - lastTime);
        lastTime = now;
        if (dt > 0.05f) dt = 0.05f; // avoid huge physics steps after stalls

        glfwPollEvents();
        pollMovement();

        // --- Update ---
        app.player.update(world, app.input, dt);
        world.update(app.player.pos, settings.renderDistance);
        world.processMeshing(8);

        glm::vec3 eye = app.player.eyePos();
        glm::vec3 dir = app.player.lookDir();
        RaycastHit hit = world.raycast(eye, dir, REACH);

        if (app.breakPressed && hit.hit &&
            isBreakable(world.getBlock(hit.block.x, hit.block.y, hit.block.z))) {
            world.setBlock(hit.block.x, hit.block.y, hit.block.z, Block::Air);
        }
        if (app.placePressed && hit.hit) {
            glm::ivec3 p = hit.adjacent;
            if (!isSolid(world.getBlock(p.x, p.y, p.z)) && !app.player.intersectsBlock(p)) {
                world.setBlock(p.x, p.y, p.z, heldBlock());
            }
        }
        app.breakPressed = app.placePressed = false;

        // --- Render world ---
        glfwGetFramebufferSize(app.window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(SKY_COLOR.r, SKY_COLOR.g, SKY_COLOR.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspect = height > 0 ? float(width) / float(height) : 1.0f;
        glm::mat4 proj = glm::perspective(glm::radians(settings.fov), aspect, 0.05f, 600.0f);
        glm::mat4 view = glm::lookAt(eye, eye + dir, glm::vec3(0, 1, 0));
        glm::mat4 viewProj = proj * view;

        float fogEnd = float(settings.renderDistance * CHUNK_SIZE);
        chunkShader.use();
        chunkShader.setMat4("uViewProj", viewProj);
        chunkShader.setVec3("uSky", SKY_COLOR);
        chunkShader.setFloat("uFogStart", fogEnd * 0.7f);
        chunkShader.setFloat("uFogEnd", fogEnd * 0.98f);
        chunkShader.setInt("uAtlas", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas);
        world.drawChunks(Frustum::fromMatrix(viewProj));

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
        drawCrosshair(hud, width, height);
        drawDebugOverlay(hud, world, fps, frameMs, hit);
        drawHotbar(hud, width, height);
        hud.end();

        glfwSwapBuffers(app.window);

        ++frameCount;
        if (maxFrames >= 0 && frameCount >= maxFrames) {
            saveScreenshotPPM("screenshot.ppm", width, height);
            break;
        }
    }

    world.saveAllModified();
    savePlayer();
    glfwDestroyWindow(app.window);
    glfwTerminate();
    return 0;
}
