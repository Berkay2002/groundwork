#pragma once
#include "render/Shader.h"
#include <glm/glm.hpp>

class World;
class Entities;

// Draws item entities as small spinning, bobbing textured cubes, lit by the
// world light at their cell. Uses the block texture array, which the caller
// leaves bound on unit 0 (same as chunk drawing). GL half only — all item
// logic lives in Entity.cpp.
class ItemRenderer {
public:
    ItemRenderer();
    ~ItemRenderer();
    ItemRenderer(const ItemRenderer&) = delete;
    ItemRenderer& operator=(const ItemRenderer&) = delete;

    // sunLevel: day/night scale applied to the sun light channel.
    void draw(const World& world, const Entities& entities,
              const glm::mat4& viewProj, float alpha, float time, float sunLevel);

private:
    Shader shader_;
    unsigned cubeVao_ = 0, cubeVbo_ = 0;
    unsigned billboardVao_ = 0, billboardVbo_ = 0;
    int locMVP_ = -1, locLayers_ = -1, locLight_ = -1;
};
