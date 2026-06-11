#include "ui/Hud.h"
#include "world/Block.h"
#include "render/GLCompat.h"
#include "render/Shader.h"
#include "ui/font8x8_basic.h"

namespace {
const char* UI_VS = R"(
#version 330 core
layout(location = 0) in vec2 aPos;   // pixel coords, origin top-left
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uScreen;
out vec2 vUV;
out vec4 vColor;
void main() {
    vec2 ndc = vec2(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
)";

const char* UI_FS = R"(
#version 330 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
uniform int uMode; // 0 solid, 1 textured, 2 font (red channel = alpha)
out vec4 FragColor;
void main() {
    if (uMode == 0) FragColor = vColor;
    else if (uMode == 1) FragColor = vec4(texture(uTex, vUV).rgb, 1.0) * vColor;
    else FragColor = vec4(vColor.rgb, vColor.a * texture(uTex, vUV).r);
}
)";

Shader* uiShader = nullptr; // shared program, created with first Hud

// Font atlas: 16 glyphs per row, 8 rows (ASCII 0..127), 128x64 texels.
constexpr int FONT_COLS = 16, FONT_ROWS = 8;
constexpr int FONT_W = FONT_COLS * 8, FONT_H = FONT_ROWS * 8;

unsigned createFontTexture() {
    std::vector<unsigned char> img(FONT_W * FONT_H, 0);
    for (int c = 0; c < 128; ++c) {
        int gx = (c % FONT_COLS) * 8, gy = (c / FONT_COLS) * 8;
        for (int row = 0; row < 8; ++row) {
            unsigned char bits = (unsigned char)font8x8_basic[c][row];
            for (int col = 0; col < 8; ++col)
                if (bits & (1 << col))
                    img[(gy + row) * FONT_W + gx + col] = 255;
        }
    }
    unsigned tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FONT_W, FONT_H, 0, GL_RED, GL_UNSIGNED_BYTE, img.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}
} // namespace

Hud::Hud(unsigned blockAtlas) : blockAtlas_(blockAtlas) {
    if (!uiShader) uiShader = new Shader(UI_VS, UI_FS);
    fontTex_ = createFontTexture();
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei stride = 8 * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

Hud::~Hud() {
    glDeleteTextures(1, &fontTex_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
}

void Hud::begin(int w, int h) {
    screenW_ = w;
    screenH_ = h;
    solid_.verts.clear();
    tiles_.verts.clear();
    text_.verts.clear();
}

void Hud::quad(Batch& b, float x, float y, float w, float h,
               float u0, float v0, float u1, float v1,
               float r, float g, float bl, float a) {
    const float v[6][4] = {
        {x, y, u0, v0}, {x, y + h, u0, v1}, {x + w, y + h, u1, v1},
        {x, y, u0, v0}, {x + w, y + h, u1, v1}, {x + w, y, u1, v0},
    };
    for (auto& p : v) {
        b.verts.insert(b.verts.end(), {p[0], p[1], p[2], p[3], r, g, bl, a});
    }
}

void Hud::drawRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    quad(solid_, x, y, w, h, 0, 0, 0, 0, r, g, b, a);
}

void Hud::drawText(float x, float y, float scale, const std::string& text,
                   float r, float g, float b, float a) {
    float cx = x;
    for (unsigned char c : text) {
        if (c == '\n') { y += GLYPH * scale + 2 * scale; cx = x; continue; }
        if (c >= 128) c = '?';
        if (c != ' ') {
            float u0 = (c % FONT_COLS) * 8.0f / FONT_W;
            float v0 = (c / FONT_COLS) * 8.0f / FONT_H;
            quad(text_, cx, y, GLYPH * scale, GLYPH * scale,
                 u0, v0, u0 + 8.0f / FONT_W, v0 + 8.0f / FONT_H, r, g, b, a);
        }
        cx += GLYPH * scale;
    }
}

void Hud::drawTile(float x, float y, float size, int tileIndex, float brightness) {
    float u0 = float(tileIndex) / ATLAS_TILES;
    float u1 = float(tileIndex + 1) / ATLAS_TILES;
    // Atlas v=0 is the texture's top row; flip so tiles appear upright.
    quad(tiles_, x, y, size, size, u0, 1.0f, u1, 0.0f,
         brightness, brightness, brightness, 1.0f);
}

void Hud::drawBatch(const Batch& b, int mode, unsigned tex) {
    if (b.verts.empty()) return;
    uiShader->setInt("uMode", mode);
    if (tex) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, b.verts.size() * sizeof(float), b.verts.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(b.verts.size() / 8));
}

void Hud::end() {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    uiShader->use();
    uiShader->setVec2("uScreen", (float)screenW_, (float)screenH_);
    uiShader->setInt("uTex", 0);

    glBindVertexArray(vao_);
    drawBatch(solid_, 0, 0);
    drawBatch(tiles_, 1, blockAtlas_);
    drawBatch(text_, 2, fontTex_);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}
