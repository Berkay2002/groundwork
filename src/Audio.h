#pragma once

#include "Block.h" // SoundMat: blocks pick their sound family in the registry

// Tiny sound-effect player over miniaudio's low-level device API: a fixed
// pool of voices mixed in the device callback, fed by the embedded
// recordings in Sounds.h/SoundData.h. Optional at build time (ENABLE_AUDIO,
// default on) and at runtime: if no audio device can be opened (headless CI,
// broken ALSA), the game keeps running silently — every method is safe to
// call regardless.

class Audio {
public:
    Audio() = default;
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // Open the default output device; false (and silence) on failure.
    bool init();
    // Each call picks a random recorded variant of the block's material
    // bank plus a light pitch jitter; placing reuses the break family at a
    // higher pitch and lower gain. SoundMat::None plays nothing.
    void playBreak(SoundMat m);
    void playPlace(SoundMat m);
    void playFootstep();
    void setVolume(float v); // master volume 0..1

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
