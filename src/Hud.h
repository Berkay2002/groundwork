#pragma once
#include <string>
#include <vector>

// 2D overlay renderer: bitmap-font text, solid rects, and block-atlas tiles.
// Usage per frame: begin(w, h); draw*(); end();
class Hud {
public:
    // blockAtlas: GL texture id of the block tile strip (for hotbar icons).
    explicit Hud(unsigned blockAtlas);
    ~Hud();
    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;

    void begin(int screenW, int screenH);
    // Coordinates are in pixels, origin at top-left.
    void drawText(float x, float y, float scale, const std::string& text,
                  float r = 1, float g = 1, float b = 1, float a = 1);
    void drawRect(float x, float y, float w, float h,
                  float r, float g, float b, float a);
    void drawTile(float x, float y, float size, int tileIndex, float brightness = 1.0f);
    void end();

    static constexpr float GLYPH = 8.0f; // font glyph size in texels

private:
    struct Batch { std::vector<float> verts; }; // x y u v r g b a
    Batch solid_, tiles_, text_;
    void quad(Batch& b, float x, float y, float w, float h,
              float u0, float v0, float u1, float v1,
              float r, float g, float bl, float a);
    void drawBatch(const Batch& b, int mode, unsigned tex);

    unsigned blockAtlas_ = 0, fontTex_ = 0;
    unsigned vao_ = 0, vbo_ = 0;
    unsigned prog_ = 0;
    int screenW_ = 0, screenH_ = 0;
};
