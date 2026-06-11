#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

#include "SoundData.h"

// Sound effects: real recordings (Kenney's CC0 "Impact Sounds" pack)
// embedded as raw PCM in SoundData.h — regenerate with
// tools/make_sounddata.sh. Embedding keeps the binary asset-file-free,
// same as the font and the textures. (A fully synthesized version was
// tried first and rejected by ear.)
//
// This header is the GL- and device-free decode layer so the data stays
// testable headless; Audio.cpp feeds the decoded buffers to its mixer.

constexpr int SOUND_RATE = SOUND_DATA_RATE;

// s16le mono -> float in [-1, 1], peak-normalized so every effect plays at
// a predictable loudness regardless of how the recording was mastered.
inline std::vector<float> decodeSound(const unsigned char* d, unsigned len,
                                      float peak = 0.8f) {
    std::vector<float> out(len / 2);
    float m = 0.0f;
    for (size_t i = 0; i < out.size(); ++i) {
        int16_t v = int16_t(uint16_t(d[2 * i]) | (uint16_t(d[2 * i + 1]) << 8));
        out[i] = float(v) / 32768.0f;
        m = std::max(m, std::fabs(out[i]));
    }
    if (m > 0.0f)
        for (float& v : out) v *= peak / m;
    return out;
}

// All decoded variants of one event; index matches the Sound enum
// (0 Break, 1 Place, 2 Footstep). Playing a random variant per event is
// what keeps repeated actions from sounding mechanical.
inline std::vector<std::vector<float>> soundVariants(int event) {
    std::vector<std::vector<float>> out;
    const SoundBankEntry& e = SOUND_BANK[event];
    for (int i = 0; i < e.count; ++i)
        out.push_back(decodeSound(e.variants[i].data, e.variants[i].len));
    return out;
}
