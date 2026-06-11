#pragma once

#include "render/Shader.h"
#include <glm/glm.hpp>

constexpr int BREAK_CRACK_STAGES = 10;

inline int breakStageForProgress(float progress) {
    if (progress <= 0.0f) return 0;
    if (progress >= 1.0f) return BREAK_CRACK_STAGES - 1;
    int stage = int(progress * float(BREAK_CRACK_STAGES));
    return stage < 0 ? 0 : stage >= BREAK_CRACK_STAGES ? BREAK_CRACK_STAGES - 1 : stage;
}

inline int breakFaceForAdjacent(glm::ivec3 block, glm::ivec3 adjacent) {
    glm::ivec3 d = adjacent - block;
    if (d == glm::ivec3(1, 0, 0)) return 0;
    if (d == glm::ivec3(-1, 0, 0)) return 1;
    if (d == glm::ivec3(0, 1, 0)) return 2;
    if (d == glm::ivec3(0, -1, 0)) return 3;
    if (d == glm::ivec3(0, 0, 1)) return 4;
    if (d == glm::ivec3(0, 0, -1)) return 5;
    return -1;
}

class BreakOverlay {
public:
    explicit BreakOverlay(unsigned crackTexture);
    ~BreakOverlay();
    BreakOverlay(const BreakOverlay&) = delete;
    BreakOverlay& operator=(const BreakOverlay&) = delete;

    void draw(const glm::mat4& viewProj, glm::ivec3 block, glm::ivec3 adjacent,
              float progress);

private:
    Shader shader_;
    unsigned crackTexture_ = 0;
    unsigned vao_ = 0;
    unsigned vbo_ = 0;
    int locViewProj_ = -1;
    int locStage_ = -1;
};
