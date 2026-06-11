#pragma once
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

// Day/night cycle: pure functions of the world's day clock (seconds), so
// they stay headless-testable. The world's light *data* never changes with
// time of day — the renderer scales the sun channel of the baked vertex
// light by sunLevelAt() instead, so no relighting or remeshing happens.
//
// One day is DAY_LENGTH seconds. As a fraction u of the day:
//   0.00..0.40 daytime, 0.40..0.50 dusk, 0.50..0.90 night, 0.90..1.00 dawn.
// A new world starts at u = 0 (morning).

constexpr float DAY_LENGTH = 600.0f; // seconds per full day (10 min)
constexpr float NIGHT_SUN = 0.15f;   // moonlight: sun scale floor at night

inline float dayFraction(float t) {
    float u = std::fmod(t / DAY_LENGTH, 1.0f);
    return u < 0.0f ? u + 1.0f : u;
}

// 1 during the day, 0 at night, smoothstepped through dusk/dawn.
inline float dayFactor(float u) {
    auto smooth = [](float a, float b, float x) {
        float s = std::min(1.0f, std::max(0.0f, (x - a) / (b - a)));
        return s * s * (3.0f - 2.0f * s);
    };
    if (u < 0.40f) return 1.0f;
    if (u < 0.50f) return 1.0f - smooth(0.40f, 0.50f, u);
    if (u < 0.90f) return 0.0f;
    return smooth(0.90f, 1.00f, u);
}

// Multiplier for the sun-light channel of baked vertex light.
inline float sunLevelAt(float t) {
    return NIGHT_SUN + (1.0f - NIGHT_SUN) * dayFactor(dayFraction(t));
}

// Sky (and fog — the chunk shader fades to uSky) color over the day:
// day blue <-> night near-black, blended toward an orange horizon tint
// that peaks mid-dusk/mid-dawn.
inline glm::vec3 skyColorAt(float t) {
    float d = dayFactor(dayFraction(t));
    const glm::vec3 day(0.53f, 0.71f, 0.92f);
    const glm::vec3 night(0.012f, 0.018f, 0.055f);
    const glm::vec3 horizon(0.93f, 0.49f, 0.26f);
    glm::vec3 c = night + (day - night) * d;
    float w = 4.0f * d * (1.0f - d) * 0.55f;
    return c + (horizon - c) * w;
}
