#pragma once
#include "render/Shader.h"
#include "sim/Item.h"
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

    // sunLevel: day/night scale applied to the sun light channel. eye is
    // the camera position: block-item cubes spin, but flat sprite items
    // billboard toward the camera (an edge-on flat quad disappears).
    void draw(const World& world, const Entities& entities,
              const glm::mat4& viewProj, const glm::vec3& eye, float alpha,
              float time, float sunLevel);

    // First-person viewmodel, drawn after all world passes and before the
    // HUD (it clears the depth buffer so the item never clips into walls).
    // Renders the held stack bottom-right: block items as a mini cube,
    // other items as their flat icon sprite, an empty hand as the arm
    // cuboid. swing is a 0..1 animation phase (0 = at rest), lit by the
    // world light at the player's eye cell.
    void drawHeld(const World& world, const ItemStack& stack,
                  const glm::vec3& eye, float aspect, float sunLevel,
                  float swing);

private:
    Shader shader_;
    unsigned cubeVao_ = 0, cubeVbo_ = 0;
    unsigned billboardVao_ = 0, billboardVbo_ = 0;
    int locMVP_ = -1, locLayers_ = -1, locLight_ = -1;
};
