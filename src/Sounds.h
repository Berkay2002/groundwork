#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

// Procedurally synthesized sound effects (the "zero asset files" rule applies
// to audio too). Pure functions of nothing but a fixed sample rate — GL- and
// device-free so they stay testable headless; Audio.cpp feeds them to the
// mixer. All samples are mono float32 in [-1, 1].

constexpr int SOUND_RATE = 44100;

namespace sounddetail {

// Deterministic noise source (xorshift32) so synthesis is reproducible.
struct NoiseGen {
    uint32_t s = 0x12345678u;
    float next() { // uniform in [-1, 1]
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return float(int32_t(s)) * (1.0f / 2147483648.0f);
    }
};

// One-pole lowpass: the standard cheap way to turn white noise into a
// material-ish "thump" (cutoff in Hz).
struct LowPass {
    float a, y = 0.0f;
    explicit LowPass(float cutoffHz) {
        float x = 2.0f * 3.14159265f * cutoffHz / SOUND_RATE;
        a = x / (x + 1.0f);
    }
    float next(float in) { return y += a * (in - y); }
};

} // namespace sounddetail

// Block break: a sharp crack decaying into rubble noise.
inline std::vector<float> makeBreakSound() {
    using namespace sounddetail;
    const int n = SOUND_RATE * 14 / 100; // 140 ms
    std::vector<float> out(n);
    NoiseGen rng;
    LowPass lp(2200.0f);
    for (int i = 0; i < n; ++i) {
        float t = float(i) / n;
        float env = std::exp(-7.0f * t);
        float crack = t < 0.04f ? 1.6f : 1.0f; // initial transient
        out[i] = lp.next(rng.next()) * env * crack * 0.9f;
    }
    return out;
}

// Block place: a soft knock — lowpassed noise plus a short 170 Hz body.
inline std::vector<float> makePlaceSound() {
    using namespace sounddetail;
    const int n = SOUND_RATE * 8 / 100; // 80 ms
    std::vector<float> out(n);
    NoiseGen rng{0xBEEF1234u};
    LowPass lp(750.0f);
    for (int i = 0; i < n; ++i) {
        float t = float(i) / n;
        float env = std::exp(-10.0f * t);
        float knock = std::sin(2.0f * 3.14159265f * 170.0f * i / SOUND_RATE);
        out[i] = (lp.next(rng.next()) * 0.7f + knock * 0.35f) * env;
    }
    return out;
}

// Footstep: a quiet, dull noise tap.
inline std::vector<float> makeFootstepSound() {
    using namespace sounddetail;
    const int n = SOUND_RATE * 7 / 100; // 70 ms
    std::vector<float> out(n);
    NoiseGen rng{0xF00757EBu};
    LowPass lp(900.0f);
    for (int i = 0; i < n; ++i) {
        float t = float(i) / n;
        float env = (1.0f - t) * std::exp(-6.0f * t);
        out[i] = lp.next(rng.next()) * env * 0.5f;
    }
    return out;
}
