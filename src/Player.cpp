#include "Player.h"
#include "World.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr float GRAVITY = -28.0f;
constexpr float JUMP_SPEED = 9.2f;
constexpr float WALK_SPEED = 4.5f;
constexpr float SPRINT_SPEED = 7.0f;
constexpr float FLY_SPEED = 12.0f;
constexpr float TERMINAL = -60.0f;
// In water: weak gravity, slow sinking, and holding jump swims upward —
// without this a lake deeper than ~1.5 blocks would be inescapable.
constexpr float WATER_GRAVITY = -10.0f;
constexpr float WATER_TERMINAL = -4.0f;
constexpr float SWIM_SPEED = 4.5f;
}

glm::vec3 Player::lookDir() const {
    float cy = std::cos(glm::radians(yaw)), sy = std::sin(glm::radians(yaw));
    float cp = std::cos(glm::radians(pitch)), sp = std::sin(glm::radians(pitch));
    return glm::normalize(glm::vec3(cy * cp, sp, sy * cp));
}

void Player::look(float dx, float dy) {
    yaw += dx * sensitivity;
    pitch -= dy * sensitivity;
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
}

bool Player::collidesAt(World& world, const glm::vec3& p) const {
    const float hw = WIDTH * 0.5f;
    int x0 = (int)std::floor(p.x - hw), x1 = (int)std::floor(p.x + hw - 1e-5f);
    int y0 = (int)std::floor(p.y),      y1 = (int)std::floor(p.y + HEIGHT - 1e-5f);
    int z0 = (int)std::floor(p.z - hw), z1 = (int)std::floor(p.z + hw - 1e-5f);
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                if (isCollidable(world.getBlock(x, y, z))) return true;
    return false;
}

bool Player::intersectsBlock(const glm::ivec3& b) const {
    const float hw = WIDTH * 0.5f;
    return pos.x + hw > b.x && pos.x - hw < b.x + 1 &&
           pos.y + HEIGHT > b.y && pos.y < b.y + 1 &&
           pos.z + hw > b.z && pos.z - hw < b.z + 1;
}

void Player::moveAxis(World& world, int axis, float amount) {
    // Step in small increments so fast movement can't tunnel through blocks.
    const float maxStep = 0.45f;
    while (amount != 0.0f) {
        float step = glm::clamp(amount, -maxStep, maxStep);
        amount -= step;
        glm::vec3 next = pos;
        next[axis] += step;
        if (!collidesAt(world, next)) {
            pos = next;
        } else {
            if (axis == 1) {
                if (vel.y < 0.0f) onGround = true;
                vel.y = 0.0f;
            } else {
                vel[axis] = 0.0f;
            }
            break;
        }
    }
}

void Player::update(World& world, const PlayerInput& in, float dt) {
    // Horizontal wish direction from yaw (ignore pitch for walking).
    glm::vec3 fwd(std::cos(glm::radians(yaw)), 0, std::sin(glm::radians(yaw)));
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    glm::vec3 wish(0.0f);
    if (in.forward) wish += fwd;
    if (in.back)    wish -= fwd;
    if (in.right)   wish += right;
    if (in.left)    wish -= right;
    if (glm::length(wish) > 0.0f) wish = glm::normalize(wish);

    if (flying) {
        float speed = FLY_SPEED * (in.sprint ? 2.0f : 1.0f);
        vel = wish * speed;
        if (in.jump)  vel.y = speed;
        if (in.sneak) vel.y = -speed;
    } else {
        // Swimming when the body's center is in water.
        bool inWater = world.getBlock((int)std::floor(pos.x),
                                      (int)std::floor(pos.y + HEIGHT * 0.5f),
                                      (int)std::floor(pos.z)) == Block::Water;
        float speed = in.sprint ? SPRINT_SPEED : WALK_SPEED;
        if (inWater) speed *= 0.6f;
        vel.x = wish.x * speed;
        vel.z = wish.z * speed;
        vel.y += (inWater ? WATER_GRAVITY : GRAVITY) * dt;
        float terminal = inWater ? WATER_TERMINAL : TERMINAL;
        if (vel.y < terminal) vel.y = terminal;
        if (in.jump) {
            if (inWater) {
                vel.y = SWIM_SPEED;
            } else if (onGround) {
                vel.y = JUMP_SPEED;
                onGround = false;
            }
        }
    }

    onGround = false;
    moveAxis(world, 1, vel.y * dt); // Y first for stable ground detection
    moveAxis(world, 0, vel.x * dt);
    moveAxis(world, 2, vel.z * dt);

    // Safety net: fell out of the world.
    if (pos.y < -20.0f) spawn(world);
}

void Player::ensureNotStuck(World& world) {
    if (collidesAt(world, pos)) spawn(world);
}

void Player::spawn(World& world) {
    int x = (int)std::floor(pos.x), z = (int)std::floor(pos.z);
    for (int y = CHUNK_HEIGHT - 1; y >= 0; --y) {
        if (isSolid(world.getBlock(x, y, z))) {
            pos = glm::vec3(x + 0.5f, float(y + 1), z + 0.5f);
            vel = glm::vec3(0.0f);
            return;
        }
    }
    pos = glm::vec3(x + 0.5f, float(CHUNK_HEIGHT), z + 0.5f);
    vel = glm::vec3(0.0f);
}
