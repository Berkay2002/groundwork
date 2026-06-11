#include "sim/Physics.h"
#include "world/World.h"
#include <cmath>

bool bodyCollidesAt(const World& world, const Body& b, const glm::vec3& p) {
    const float hw = b.halfWidth;
    int x0 = (int)std::floor(p.x - hw), x1 = (int)std::floor(p.x + hw - 1e-5f);
    int y0 = (int)std::floor(p.y),      y1 = (int)std::floor(p.y + b.height - 1e-5f);
    int z0 = (int)std::floor(p.z - hw), z1 = (int)std::floor(p.z + hw - 1e-5f);
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                if (isCollidable(world.getBlock(x, y, z))) return true;
    return false;
}

void moveBodyAxis(const World& world, Body& b, int axis, float amount) {
    // Step in small increments so fast movement can't tunnel through blocks.
    const float maxStep = 0.45f;
    while (amount != 0.0f) {
        float step = glm::clamp(amount, -maxStep, maxStep);
        amount -= step;
        glm::vec3 next = b.pos;
        next[axis] += step;
        if (!bodyCollidesAt(world, b, next)) {
            b.pos = next;
        } else {
            // Snap flush against the obstacle instead of stopping at the
            // last sub-step, so bodies rest exactly on surfaces. A step is
            // <= 0.45 < 1 block, so the leading face crossed at most one
            // cell boundary — the obstacle plane is that boundary.
            const float low = (axis == 1) ? 0.0f : -b.halfWidth;
            const float high = (axis == 1) ? b.height : b.halfWidth;
            glm::vec3 flush = b.pos;
            if (step > 0.0f) {
                int cell = (int)std::floor(next[axis] + high - 1e-5f);
                flush[axis] = float(cell) - high;
            } else {
                int cell = (int)std::floor(next[axis] + low);
                flush[axis] = float(cell + 1) - low;
            }
            if (!bodyCollidesAt(world, b, flush)) b.pos = flush;
            if (axis == 1) {
                if (b.vel.y < 0.0f) b.onGround = true;
                b.vel.y = 0.0f;
            } else {
                b.vel[axis] = 0.0f;
            }
            break;
        }
    }
}

void moveBody(const World& world, Body& b, float dt) {
    b.onGround = false;
    moveBodyAxis(world, b, 1, b.vel.y * dt); // Y first for ground detection
    moveBodyAxis(world, b, 0, b.vel.x * dt);
    moveBodyAxis(world, b, 2, b.vel.z * dt);
}
