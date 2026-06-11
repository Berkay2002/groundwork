#pragma once
#include <glm/glm.hpp>

class World;

// Axis-aligned physics body shared by the player and entities: pos is the
// feet center (center in X/Z, bottom in Y), exactly like the player always
// worked. Movement is per-axis and sub-stepped so nothing tunnels.
struct Body {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    float halfWidth = 0.3f;
    float height = 1.8f;
    bool onGround = false;
};

bool bodyCollidesAt(const World& world, const Body& b, const glm::vec3& p);
// Move along one axis in <=0.45-block steps; a collision zeroes that
// velocity component (and sets onGround when landing on Y).
void moveBodyAxis(const World& world, Body& b, int axis, float amount);
// Y first (stable ground detection), then X, Z. Clears onGround first.
void moveBody(const World& world, Body& b, float dt);
