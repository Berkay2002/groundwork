#pragma once

// Tiny sound-effect player over miniaudio's low-level device API: a fixed
// pool of voices mixed in the device callback, fed by the procedural buffers
// in Sounds.h. Optional at build time (ENABLE_AUDIO, default on) and at
// runtime: if no audio device can be opened (headless CI, broken ALSA), the
// game keeps running silently — every method is safe to call regardless.

enum class Sound { Break, Place, Footstep };

class Audio {
public:
    Audio() = default;
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // Open the default output device; false (and silence) on failure.
    bool init();
    // gain: per-play volume 0..1. pitch: playback-rate multiplier; small
    // variation (0.9..1.1) keeps repeated effects from sounding mechanical.
    void play(Sound s, float gain = 1.0f, float pitch = 1.0f);
    // pitch varied automatically per call (deterministic LCG).
    void playVaried(Sound s, float gain = 1.0f);
    void setVolume(float v); // master volume 0..1

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
