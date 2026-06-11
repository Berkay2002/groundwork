#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

// Procedurally synthesized sound effects (the "zero asset files" rule applies
// to audio too). Pure functions of nothing but a fixed sample rate — GL- and
// device-free so they stay testable headless; Audio.cpp feeds them to the
// mixer. All samples are mono float32 in [-1, 1].
//
// Design notes (first version was harsh — user feedback): everything is
// built from *brown* noise (integrated white, energy falls 6 dB/octave, the
// "dull rumble" end of the noise family) instead of hissy white noise, every
// envelope has a short attack ramp so nothing starts or stops with a click,
// and the cutoffs sit low. Material thuds come from a damped, downward-
// gliding sine, not from noise.

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

// Brown noise: a leaky integrator over white noise. Much softer on the ear
// than white noise — no high-frequency hiss to begin with.
struct BrownGen {
    NoiseGen rng;
    float y = 0.0f;
    explicit BrownGen(uint32_t seed) { rng.s = seed; }
    float next() {
        y = 0.985f * y + 0.06f * rng.next();
        return y * 4.0f; // roughly renormalize to [-1, 1]
    }
};

// One-pole lowpass (cutoff in Hz) to shape the remaining top end.
struct LowPass {
    float a, y = 0.0f;
    explicit LowPass(float cutoffHz) {
        float x = 2.0f * 3.14159265f * cutoffHz / SOUND_RATE;
        a = x / (x + 1.0f);
    }
    float next(float in) { return y += a * (in - y); }
};

// Attack/decay envelope: a short linear fade-in (no onset click), an
// exponential body, and a forced linear fade-out over the final 10 ms so
// the buffer always ends in true silence.
inline float envelope(int i, int n, float attackMs, float decayRate) {
    float t = float(i) / SOUND_RATE;
    float e = std::exp(-decayRate * t);
    float atk = attackMs * 0.001f;
    if (t < atk) e *= t / atk;
    int fade = SOUND_RATE / 100; // 10 ms
    if (i > n - fade) e *= float(n - i) / fade;
    return e;
}

// Scale a buffer so its peak is exactly `peak` (brown noise is a random
// walk — its raw amplitude isn't predictable, so post-normalize).
inline void normalize(std::vector<float>& s, float peak) {
    float m = 0.0f;
    for (float v : s) m = std::max(m, std::fabs(v));
    if (m > 0.0f)
        for (float& v : s) v *= peak / m;
}

} // namespace sounddetail

// Block break: a dull crumble — brown noise with a quickly-decaying body,
// like dirt giving way. No transient spike (a step in amplitude is a click).
inline std::vector<float> makeBreakSound() {
    using namespace sounddetail;
    const int n = SOUND_RATE * 16 / 100; // 160 ms
    std::vector<float> out(n);
    BrownGen brown(0xC0FFEE11u);
    LowPass lp(1100.0f);
    for (int i = 0; i < n; ++i)
        out[i] = lp.next(brown.next()) * envelope(i, n, 4.0f, 16.0f);
    normalize(out, 0.75f);
    return out;
}

// Block place: a soft "tok" — a damped sine that glides downward (the pitch
// drop is what reads as a knock on wood rather than an electronic beep),
// with a whisper of brown noise for texture.
inline std::vector<float> makePlaceSound() {
    using namespace sounddetail;
    const int n = SOUND_RATE * 9 / 100; // 90 ms
    std::vector<float> out(n);
    BrownGen brown(0xBEEF1234u);
    LowPass lp(600.0f);
    float phase = 0.0f;
    for (int i = 0; i < n; ++i) {
        float t = float(i) / SOUND_RATE;
        float freq = 190.0f * std::exp(-8.0f * t) + 70.0f; // 260 Hz -> 70 Hz
        phase += 2.0f * 3.14159265f * freq / SOUND_RATE;
        float knock = std::sin(phase);
        float e = envelope(i, n, 2.0f, 26.0f);
        out[i] = (knock * 0.55f + lp.next(brown.next()) * 0.15f) * e;
    }
    normalize(out, 0.6f);
    return out;
}

// Footstep: a very quiet, muffled scuff. Deliberately understated — it
// repeats every couple of meters, so it must sit far in the background.
inline std::vector<float> makeFootstepSound() {
    using namespace sounddetail;
    const int n = SOUND_RATE * 6 / 100; // 60 ms
    std::vector<float> out(n);
    BrownGen brown(0xF00757EBu);
    LowPass lp(420.0f);
    for (int i = 0; i < n; ++i)
        out[i] = lp.next(brown.next()) * envelope(i, n, 6.0f, 30.0f);
    normalize(out, 0.35f);
    return out;
}
