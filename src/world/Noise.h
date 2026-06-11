#pragma once
#include <cstdint>
#include <cmath>

// Deterministic 2D value noise with fBm. Same seed -> same terrain.
class Noise {
public:
    explicit Noise(uint32_t seed) : seed_(seed) {}

    // fBm in roughly [-1, 1]
    float fbm(float x, float z, int octaves, float baseFreq) const {
        float sum = 0.0f, amp = 1.0f, norm = 0.0f, freq = baseFreq;
        for (int i = 0; i < octaves; ++i) {
            sum += amp * value(x * freq, z * freq, seed_ + uint32_t(i) * 0x9E3779B9u);
            norm += amp;
            amp *= 0.5f;
            freq *= 2.0f;
        }
        return sum / norm;
    }

    // 3D fBm in roughly [-1, 1] (used for cave carving).
    float fbm3(float x, float y, float z, int octaves, float baseFreq) const {
        float sum = 0.0f, amp = 1.0f, norm = 0.0f, freq = baseFreq;
        for (int i = 0; i < octaves; ++i) {
            sum += amp * value3(x * freq, y * freq, z * freq, seed_ + uint32_t(i) * 0x9E3779B9u);
            norm += amp;
            amp *= 0.5f;
            freq *= 2.0f;
        }
        return sum / norm;
    }

private:
    uint32_t seed_;

    static uint32_t hash(int32_t x, int32_t z, uint32_t seed) {
        uint32_t h = seed;
        h ^= uint32_t(x) * 0x85EBCA6Bu;
        h = (h << 13) | (h >> 19);
        h ^= uint32_t(z) * 0xC2B2AE35u;
        h *= 0x27D4EB2Fu;
        h ^= h >> 15;
        return h;
    }

    // Lattice value in [-1, 1]
    static float lattice(int32_t x, int32_t z, uint32_t seed) {
        return (hash(x, z, seed) & 0xFFFFFF) / float(0x7FFFFF) - 1.0f;
    }

    static float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

    static float value(float x, float z, uint32_t seed) {
        int32_t x0 = (int32_t)std::floor(x), z0 = (int32_t)std::floor(z);
        float tx = smooth(x - x0), tz = smooth(z - z0);
        float a = lattice(x0, z0, seed),     b = lattice(x0 + 1, z0, seed);
        float c = lattice(x0, z0 + 1, seed), d = lattice(x0 + 1, z0 + 1, seed);
        float top = a + (b - a) * tx;
        float bot = c + (d - c) * tx;
        return top + (bot - top) * tz;
    }

    // 3D lattice reuses the 2D hash with y folded into the seed.
    static float lattice3(int32_t x, int32_t y, int32_t z, uint32_t seed) {
        return lattice(x, z, seed ^ (uint32_t(y) * 0x9E3779B9u));
    }

    static float value3(float x, float y, float z, uint32_t seed) {
        int32_t x0 = (int32_t)std::floor(x), y0 = (int32_t)std::floor(y),
                z0 = (int32_t)std::floor(z);
        float tx = smooth(x - x0), ty = smooth(y - y0), tz = smooth(z - z0);
        auto plane = [&](int32_t yy) {
            float a = lattice3(x0, yy, z0, seed),     b = lattice3(x0 + 1, yy, z0, seed);
            float c = lattice3(x0, yy, z0 + 1, seed), d = lattice3(x0 + 1, yy, z0 + 1, seed);
            float top = a + (b - a) * tx;
            float bot = c + (d - c) * tx;
            return top + (bot - top) * tz;
        };
        float lo = plane(y0), hi = plane(y0 + 1);
        return lo + (hi - lo) * ty;
    }
};
