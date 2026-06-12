#include "render/ItemRenderer.h"
#include "world/Block.h"
#include "sim/Entity.h"
#include "render/Texture.h"
#include "world/World.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace {
const char* ITEM_VS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in float aFace;
layout(location = 3) in float aShade;
uniform mat4 uMVP;
uniform float uLight;
out vec2 vUV;
flat out int vFace;
out float vLight;
void main() {
    vUV = aUV;
    vFace = int(aFace + 0.5);
    vLight = aShade * uLight;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* ITEM_FS = R"(
#version 330 core
in vec2 vUV;
flat in int vFace;
in float vLight;
uniform sampler2DArray uAtlas;
uniform float uLayers[6];
out vec4 FragColor;
void main() {
    vec4 c = texture(uAtlas, vec3(vUV, uLayers[vFace]));
    if (c.a < 0.5) discard; // item sprites are cut-outs; block tiles are opaque
    FragColor = vec4(c.rgb * vLight, 1.0);
}
)";

// Unit cube, x/z in [-0.5,0.5], y in [0,1] (feet origin like Body.pos).
// Face order matches Block.h tiles: +X -X +Y -Y +Z -Z. Drawn with face
// culling disabled (a handful of cubes), so winding doesn't matter.
// Tile-space v=1 is the visual top of a tile (see Texture.cpp), so the top
// vertices of a face sample v=1.
std::vector<float> buildCube() {
    std::vector<float> v;
    auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                    int face, float shade) {
        const glm::vec3 P[6] = {a, b, c, a, c, d};
        const float U[6] = {0, 1, 1, 0, 1, 0};
        const float W[6] = {0, 0, 1, 0, 1, 1};
        for (int i = 0; i < 6; ++i)
            v.insert(v.end(), {P[i].x, P[i].y, P[i].z, U[i], W[i],
                               float(face), shade});
    };
    const float l = -0.5f, r = 0.5f, b = 0.0f, t = 1.0f;
    quad({r, b, l}, {r, b, r}, {r, t, r}, {r, t, l}, 0, 0.80f); // +X
    quad({l, b, r}, {l, b, l}, {l, t, l}, {l, t, r}, 1, 0.80f); // -X
    quad({l, t, l}, {r, t, l}, {r, t, r}, {l, t, r}, 2, 1.00f); // +Y
    quad({l, b, r}, {r, b, r}, {r, b, l}, {l, b, l}, 3, 0.60f); // -Y
    quad({r, b, r}, {l, b, r}, {l, t, r}, {r, t, r}, 4, 0.70f); // +Z
    quad({l, b, l}, {r, b, l}, {r, t, l}, {l, t, l}, 5, 0.70f); // -Z
    return v;
}

std::vector<float> buildBillboard() {
    std::vector<float> v;
    const glm::vec3 P[6] = {
        {-0.5f, 0.0f, 0.0f}, {0.5f, 0.0f, 0.0f}, {0.5f, 1.0f, 0.0f},
        {-0.5f, 0.0f, 0.0f}, {0.5f, 1.0f, 0.0f}, {-0.5f, 1.0f, 0.0f},
    };
    const float U[6] = {0, 1, 1, 0, 1, 0};
    const float W[6] = {0, 0, 1, 0, 1, 1};
    for (int i = 0; i < 6; ++i)
        v.insert(v.end(), {P[i].x, P[i].y, P[i].z, U[i], W[i], 0.0f, 1.0f});
    return v;
}

void uploadItemGeometry(unsigned& vao, unsigned& vbo, const std::vector<float>& verts) {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(),
                 GL_STATIC_DRAW);
    const GLsizei stride = 7 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    for (int i = 0; i < 4; ++i) glEnableVertexAttribArray(i);
    glBindVertexArray(0);
}
} // namespace

ItemRenderer::ItemRenderer() : shader_(ITEM_VS, ITEM_FS) {
    locMVP_ = shader_.loc("uMVP");
    locLayers_ = shader_.loc("uLayers");
    locLight_ = shader_.loc("uLight");
    shader_.use();
    shader_.setInt("uAtlas", 0);

    uploadItemGeometry(cubeVao_, cubeVbo_, buildCube());
    uploadItemGeometry(billboardVao_, billboardVbo_, buildBillboard());
}

ItemRenderer::~ItemRenderer() {
    glDeleteBuffers(1, &cubeVbo_);
    glDeleteVertexArrays(1, &cubeVao_);
    glDeleteBuffers(1, &billboardVbo_);
    glDeleteVertexArrays(1, &billboardVao_);
}

void ItemRenderer::draw(const World& world, const Entities& entities,
                        const glm::mat4& viewProj, const glm::vec3& eye,
                        float alpha, float time, float sunLevel,
                        float heldLight) {
    if (entities.items().empty()) return;
    shader_.use();
    glDisable(GL_CULL_FACE);
    for (const auto& up : entities.items()) {
        const ItemEntity& e = *up;
        glm::vec3 p = e.renderPos(alpha);
        float phase = float(e.spinSeed % 628u) * 0.01f;
        float bob = 0.06f + 0.05f * std::sin(time * 2.0f + phase);
        glm::mat4 m = glm::translate(glm::mat4(1.0f), p + glm::vec3(0, bob, 0));
        bool cube = itemUsesBlockCube(e.stack.item);
        if (cube) {
            m = glm::rotate(m, time * 1.5f + phase, glm::vec3(0, 1, 0));
        } else {
            // Flat sprites never spin edge-on: face the camera (Minecraft
            // dropped-item feel), with a slight fixed tilt for depth.
            m = glm::rotate(m, std::atan2(eye.x - p.x, eye.z - p.z),
                            glm::vec3(0, 1, 0));
            m = glm::rotate(m, glm::radians(-12.0f), glm::vec3(1, 0, 0));
        }
        m = glm::scale(m, glm::vec3(cube ? 0.25f : 0.32f));
        glm::mat4 mvp = viewProj * m;
        glUniformMatrix4fv(locMVP_, 1, GL_FALSE, glm::value_ptr(mvp));
        float layers[6];
        if (cube) {
            Block b = placeBlockForItem(e.stack.item);
            for (int f = 0; f < 6; ++f) layers[f] = float(tileFor(b, f));
        } else {
            layers[0] = float(itemIconTile(e.stack.item));
            for (int f = 1; f < 6; ++f) layers[f] = layers[0];
        }
        glUniform1fv(locLayers_, 6, layers);
        int wx = (int)std::floor(p.x), wy = (int)std::floor(p.y + 0.2f),
            wz = (int)std::floor(p.z);
        // Same channel split as chunk lighting: sunlight dims with the
        // day/night cycle, torch light doesn't.
        float sunBr = std::pow(0.85f, float(15 - world.sunLightAt(wx, wy, wz)));
        float blkBr = std::pow(0.85f, float(15 - world.blockLightAt(wx, wy, wz)));
        // Hand-light falloff (matches the chunk shader's uHeldLight) plus
        // self-glow: a dropped torch is lit by its own emission.
        float handLvl = std::max(0.0f, heldLight - glm::distance(p, eye));
        float handBr = std::pow(0.85f, 15.0f - handLvl);
        float selfBr = std::pow(
            0.85f, float(15 - lightEmission(placeBlockForItem(e.stack.item))));
        glUniform1f(locLight_,
                    std::max({sunBr * sunLevel, blkBr, handBr, selfBr}));
        glBindVertexArray(cube ? cubeVao_ : billboardVao_);
        glDrawArrays(GL_TRIANGLES, 0, cube ? 36 : 6);
    }
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

void ItemRenderer::drawHeld(const World& world, const ItemStack& stack,
                            const glm::vec3& eye, float aspect, float sunLevel,
                            float swing) {
    bool arm = stack.empty();
    bool cube = !arm && itemUsesBlockCube(stack.item);
    // Sprite-drawn placeables (torch): held upright, flame up — the rolled
    // tool pose would lay the torch on its side.
    bool upright = !arm && !cube && placeBlockForItem(stack.item) != Block::Air;

    shader_.use();
    glClear(GL_DEPTH_BUFFER_BIT); // viewmodel never clips into world geometry
    glDisable(GL_CULL_FACE);

    // View space directly (no view matrix): x right, y up, -z forward.
    glm::mat4 proj = glm::perspective(glm::radians(62.0f), aspect, 0.05f, 4.0f);
    float s = std::sin(swing * 3.14159265f); // 0..1..0 chop arc
    glm::mat4 m(1.0f);
    m = glm::translate(m, cube || arm || upright
                              ? glm::vec3(0.78f - 0.26f * s, -0.68f - 0.2f * s, -1.15f)
                              : glm::vec3(1.0f - 0.3f * s, -0.48f - 0.22f * s, -1.15f));
    m = glm::rotate(m, glm::radians(-70.0f) * s, glm::vec3(1, 0, 0));
    if (upright) {
        // Single upright sprite, slightly yawed so it isn't edge-on, tilted
        // a touch toward the view like Minecraft's held torch.
        m = glm::rotate(m, glm::radians(-35.0f), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(12.0f), glm::vec3(0, 0, 1));
        m = glm::scale(m, glm::vec3(0.75f));
        m = glm::translate(m, glm::vec3(0, -0.35f, 0));
    } else if (cube) {
        // Mini block held at a Minecraft-like angle.
        m = glm::rotate(m, glm::radians(40.0f), glm::vec3(0, 1, 0));
        m = glm::scale(m, glm::vec3(0.4f));
        m = glm::translate(m, glm::vec3(0, -0.5f, 0));
    } else if (arm) {
        // Bare arm: a long cuboid reaching from the bottom-right up-forward.
        m = glm::rotate(m, glm::radians(-62.0f), glm::vec3(1, 0, 0));
        m = glm::rotate(m, glm::radians(-18.0f), glm::vec3(0, 0, 1));
        m = glm::scale(m, glm::vec3(0.34f, 1.5f, 0.34f));
        m = glm::translate(m, glm::vec3(0, -0.85f, 0));
    } else {
        // Flat icon sprite in the classic first-person pose: rolled 90 so
        // the handle runs off the bottom-right corner and the tool head
        // leads upper-left (matches the vanilla held-pickaxe reference).
        // Strong negative yaw: the sprite plane recedes into the scene —
        // handle near the player's shoulder, tool head angled forward and
        // away (top-down sketch reference, 2026-06-12), not a flat card.
        m = glm::rotate(m, glm::radians(-65.0f), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0, 0, 1));
        m = glm::scale(m, glm::vec3(0.9f));
        m = glm::translate(m, glm::vec3(0, -0.5f, 0));
    }
    glm::mat4 mvp = proj * m;
    glUniformMatrix4fv(locMVP_, 1, GL_FALSE, glm::value_ptr(mvp));

    float layers[6];
    if (cube) {
        Block b = placeBlockForItem(stack.item);
        for (int f = 0; f < 6; ++f) layers[f] = float(tileFor(b, f));
    } else {
        layers[0] = arm ? float(TileId::PlayerArm)
                        : float(itemIconTile(stack.item));
        for (int f = 1; f < 6; ++f) layers[f] = layers[0];
    }
    glUniform1fv(locLayers_, 6, layers);

    int wx = (int)std::floor(eye.x), wy = (int)std::floor(eye.y),
        wz = (int)std::floor(eye.z);
    float sunBr = std::pow(0.85f, float(15 - world.sunLightAt(wx, wy, wz)));
    float blkBr = std::pow(0.85f, float(15 - world.blockLightAt(wx, wy, wz)));
    // An emissive held item (torch) lights itself: never darker than its
    // own emission, so the viewmodel flame glows in a pitch-black cave.
    float selfBr = arm ? 0.0f
                       : std::pow(0.85f, float(15 - lightEmission(
                                              placeBlockForItem(stack.item))));
    glUniform1f(locLight_, std::max({sunBr * sunLevel, blkBr, selfBr}));

    bool useCubeVao = cube || arm;
    glBindVertexArray(useCubeVao ? cubeVao_ : billboardVao_);
    glDrawArrays(GL_TRIANGLES, 0, useCubeVao ? 36 : 6);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}
