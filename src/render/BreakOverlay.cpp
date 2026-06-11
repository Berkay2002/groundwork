#include "render/BreakOverlay.h"
#include "render/GLCompat.h"
#include <array>
#include <glm/gtc/type_ptr.hpp>

namespace {
const char* BREAK_VS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uViewProj;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

const char* BREAK_FS = R"(
#version 330 core
in vec2 vUV;
uniform sampler2DArray uCracks;
uniform int uStage;
out vec4 FragColor;
void main() {
    float a = texture(uCracks, vec3(vUV, float(uStage))).a;
    FragColor = vec4(0.02, 0.018, 0.015, a * 0.82);
}
)";

using VertArray = std::array<float, 30>; // 6 verts * (xyz, uv)

void pushVertex(VertArray& out, int& i, glm::vec3 p, float u, float v) {
    out[i++] = p.x;
    out[i++] = p.y;
    out[i++] = p.z;
    out[i++] = u;
    out[i++] = v;
}

VertArray faceVertices(glm::ivec3 block, int face) {
    constexpr float e = 0.002f;
    glm::vec3 b(block);
    float x0 = b.x, x1 = b.x + 1.0f;
    float y0 = b.y, y1 = b.y + 1.0f;
    float z0 = b.z, z1 = b.z + 1.0f;
    std::array<glm::vec3, 4> q{};
    switch (face) {
        case 0: q = {{{x1 + e, y0, z0}, {x1 + e, y0, z1}, {x1 + e, y1, z1}, {x1 + e, y1, z0}}}; break;
        case 1: q = {{{x0 - e, y0, z1}, {x0 - e, y0, z0}, {x0 - e, y1, z0}, {x0 - e, y1, z1}}}; break;
        case 2: q = {{{x0, y1 + e, z0}, {x1, y1 + e, z0}, {x1, y1 + e, z1}, {x0, y1 + e, z1}}}; break;
        case 3: q = {{{x0, y0 - e, z1}, {x1, y0 - e, z1}, {x1, y0 - e, z0}, {x0, y0 - e, z0}}}; break;
        case 4: q = {{{x1, y0, z1 + e}, {x0, y0, z1 + e}, {x0, y1, z1 + e}, {x1, y1, z1 + e}}}; break;
        default: q = {{{x0, y0, z0 - e}, {x1, y0, z0 - e}, {x1, y1, z0 - e}, {x0, y1, z0 - e}}}; break;
    }

    VertArray out{};
    int i = 0;
    pushVertex(out, i, q[0], 0.0f, 1.0f);
    pushVertex(out, i, q[1], 1.0f, 1.0f);
    pushVertex(out, i, q[2], 1.0f, 0.0f);
    pushVertex(out, i, q[0], 0.0f, 1.0f);
    pushVertex(out, i, q[2], 1.0f, 0.0f);
    pushVertex(out, i, q[3], 0.0f, 0.0f);
    return out;
}
} // namespace

BreakOverlay::BreakOverlay(unsigned crackTexture)
    : shader_(BREAK_VS, BREAK_FS), crackTexture_(crackTexture) {
    locViewProj_ = shader_.loc("uViewProj");
    locStage_ = shader_.loc("uStage");
    shader_.use();
    shader_.setInt("uCracks", 0);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 30 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    const GLsizei stride = 5 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

BreakOverlay::~BreakOverlay() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
}

void BreakOverlay::draw(const glm::mat4& viewProj, glm::ivec3 block,
                        glm::ivec3 adjacent, float progress) {
    int face = breakFaceForAdjacent(block, adjacent);
    if (face < 0 || progress <= 0.0f) return;

    VertArray verts = faceVertices(block, face);
    shader_.use();
    glUniformMatrix4fv(locViewProj_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniform1i(locStage_, breakStageForProgress(progress));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, crackTexture_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(),
                 GL_DYNAMIC_DRAW);

    GLboolean cull = glIsEnabled(GL_CULL_FACE);
    GLboolean blend = glIsEnabled(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (!blend) glDisable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}
