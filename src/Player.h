#pragma once
#include <glm/glm.hpp>

class World;

struct PlayerInput {
    bool forward = false, back = false, left = false, right = false;
    bool jump = false, sneak = false, sprint = false;
};

class Player {
public:
    glm::vec3 pos{0.5f, 50.0f, 0.5f}; // feet position
    glm::vec3 prevPos{0.5f, 50.0f, 0.5f}; // position at the previous tick
    glm::vec3 vel{0.0f};
    float yaw = -90.0f;   // degrees
    float pitch = 0.0f;
    bool onGround = false;
    bool flying = false;
    float sensitivity = 0.12f;

    static constexpr float WIDTH = 0.6f;
    static constexpr float HEIGHT = 1.8f;
    static constexpr float EYE = 1.62f;

    glm::vec3 eyePos() const { return pos + glm::vec3(0, EYE, 0); }
    glm::vec3 lookDir() const;

    // Fixed-timestep interpolation: call beginTick at the start of each
    // simulation tick; render with renderPos/eyePos(alpha), alpha in [0,1).
    void beginTick() { prevPos = pos; }
    glm::vec3 renderPos(float alpha) const { return glm::mix(prevPos, pos, alpha); }
    glm::vec3 eyePos(float alpha) const { return renderPos(alpha) + glm::vec3(0, EYE, 0); }

    void look(float dx, float dy); // mouse deltas
    void update(World& world, const PlayerInput& in, float dt);

    // Snap to top of terrain at spawn.
    void spawn(World& world);
    // Respawn if currently embedded in solid blocks (e.g. terrain changed under a save).
    void ensureNotStuck(World& world);

    bool intersectsBlock(const glm::ivec3& b) const;

private:
    bool collidesAt(World& world, const glm::vec3& p) const;
};
