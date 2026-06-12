#pragma once
#include "sim/Physics.h"
#include <cstdint>
#include <glm/glm.hpp>

class World;

struct PlayerInput {
    bool forward = false, back = false, left = false, right = false;
    bool jump = false, sneak = false, sprint = false;
};

class Player {
public:
    glm::vec3 prevPos{0.5f, 50.0f, 0.5f}; // position at the previous tick
    float yaw = -90.0f;   // degrees
    float pitch = 0.0f;
    bool flying = false;
    float sensitivity = 0.12f;

    static constexpr float WIDTH = 0.6f;
    static constexpr float HEIGHT = 1.8f;
    static constexpr float EYE = 1.62f;

    // Survival health: 20 HP = 10 hearts. Damage is blocked during the brief
    // post-hit invulnerability window; health regenerates slowly once the
    // player has gone REGEN_GRACE_TICKS without being hurt. The app layer
    // owns death (respawn, inventory rules) — falling out of the world sets
    // `outOfWorld` instead of teleporting so survival mode can treat it as
    // a lethal hit.
    static constexpr int MAX_HEALTH = 20;
    static constexpr uint32_t HURT_IFRAME_TICKS = 10; // 0.5 s at 20 TPS
    static constexpr uint32_t REGEN_GRACE_TICKS = 100;   // 5 s
    static constexpr uint32_t REGEN_INTERVAL_TICKS = 60; // 1 HP / 3 s
    int health = MAX_HEALTH;
    bool outOfWorld = false;

    // Returns true if the hit landed (not absorbed by i-frames / already 0).
    bool damage(int amount);
    // Per-simulation-tick health upkeep (i-frame countdown, passive regen).
    void healthTick();
    void resetHealth();
    // Shove from a hit. Walking sets horizontal velocity directly each tick,
    // so the push is accumulated separately and decays over a few ticks.
    void applyKnockback(const glm::vec3& push);

    Body& body() { return body_; }
    const Body& body() const { return body_; }
    glm::vec3& pos() { return body_.pos; }
    const glm::vec3& pos() const { return body_.pos; }
    glm::vec3& vel() { return body_.vel; }
    const glm::vec3& vel() const { return body_.vel; }
    bool& onGround() { return body_.onGround; }
    const bool& onGround() const { return body_.onGround; }

    glm::vec3 eyePos() const { return pos() + glm::vec3(0, EYE, 0); }
    glm::vec3 lookDir() const;

    // Fixed-timestep interpolation: call beginTick at the start of each
    // simulation tick; render with renderPos/eyePos(alpha), alpha in [0,1).
    void beginTick() { prevPos = pos(); }
    glm::vec3 renderPos(float alpha) const { return glm::mix(prevPos, pos(), alpha); }
    glm::vec3 eyePos(float alpha) const { return renderPos(alpha) + glm::vec3(0, EYE, 0); }

    void look(float dx, float dy); // mouse deltas
    void update(World& world, const PlayerInput& in, float dt);

    // Snap to top of terrain at spawn.
    void spawn(World& world);
    // Respawn if currently embedded in solid blocks (e.g. terrain changed under a save).
    void ensureNotStuck(World& world);

    bool intersectsBlock(const glm::ivec3& b) const;

private:
    Body body_{glm::vec3(0.5f, 50.0f, 0.5f), glm::vec3(0.0f), WIDTH * 0.5f, HEIGHT, false};
    uint32_t ticksSinceDamage_ = REGEN_GRACE_TICKS;
    uint32_t iframeTicks_ = 0;
    uint32_t regenCounter_ = 0;
    glm::vec3 knockback_{0.0f};
    bool collidesAt(World& world, const glm::vec3& p) const;
};
