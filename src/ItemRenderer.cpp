#include "ItemRenderer.h"
#include "Block.h"
#include "Entity.h"
#include "World.h"
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
    vec3 c = texture(uAtlas, vec3(vUV, uLayers[vFace])).rgb * vLight;
    FragColor = vec4(c, 1.0);
}
)";

// Unit cube, x/z in [-0.5,0.5], y in [0,1] (feet origin like Body.pos).
// Face order matches Block.h tiles: +X -X +Y -Y +Z -Z. Drawn with face
// culling disabled (a handful of cubes), so winding doesn't matter.
std::vector<float> buildCube() {
    std::vector<float> v;
    auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                    int face, float shade) {
        const glm::vec3 P[6] = {a, b, c, a, c, d};
        const float U[6] = {0, 1, 1, 0, 1, 0};
        const float W[6] = {1, 1, 0, 1, 0, 0};
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
} // namespace

ItemRenderer::ItemRenderer() : shader_(ITEM_VS, ITEM_FS) {
    locMVP_ = shader_.loc("uMVP");
    locLayers_ = shader_.loc("uLayers");
    locLight_ = shader_.loc("uLight");
    shader_.use();
    shader_.setInt("uAtlas", 0);

    std::vector<float> verts = buildCube();
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
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

ItemRenderer::~ItemRenderer() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
}

void ItemRenderer::draw(const World& world, const Entities& entities,
                        const glm::mat4& viewProj, float alpha, float time) {
    if (entities.items().empty()) return;
    shader_.use();
    glBindVertexArray(vao_);
    glDisable(GL_CULL_FACE);
    for (const auto& up : entities.items()) {
        const ItemEntity& e = *up;
        glm::vec3 p = e.renderPos(alpha);
        float phase = float(e.spinSeed % 628u) * 0.01f;
        float bob = 0.06f + 0.05f * std::sin(time * 2.0f + phase);
        glm::mat4 m = glm::translate(glm::mat4(1.0f), p + glm::vec3(0, bob, 0));
        m = glm::rotate(m, time * 1.5f + phase, glm::vec3(0, 1, 0));
        m = glm::scale(m, glm::vec3(0.25f));
        glm::mat4 mvp = viewProj * m;
        glUniformMatrix4fv(locMVP_, 1, GL_FALSE, glm::value_ptr(mvp));
        float layers[6];
        for (int f = 0; f < 6; ++f) layers[f] = float(tileFor(e.item, f));
        glUniform1fv(locLayers_, 6, layers);
        int wx = (int)std::floor(p.x), wy = (int)std::floor(p.y + 0.2f),
            wz = (int)std::floor(p.z);
        int level = std::max(world.sunLightAt(wx, wy, wz),
                             world.blockLightAt(wx, wy, wz));
        glUniform1f(locLight_, std::pow(0.85f, float(15 - level)));
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}
