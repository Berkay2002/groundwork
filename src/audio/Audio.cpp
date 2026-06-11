#include "audio/Audio.h"

#ifdef AUDIO_ENABLED

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

#include "audio/Sounds.h"

namespace {
constexpr int MAX_VOICES = 16;

struct Voice {
    const std::vector<float>* buf = nullptr;
    float pos = 0.0f;   // sample cursor (fractional: pitch = step size)
    float step = 1.0f;
    float gain = 1.0f;
    bool active = false;
};
} // namespace

struct Audio::Impl {
    ma_device device{};
    bool deviceOk = false;
    // Decoded variants per bank (material banks + the footstep bank).
    std::vector<std::vector<float>> banks[SOUND_BANK_COUNT];
    Voice voices[MAX_VOICES];
    std::mutex voiceMutex; // guards voices between game and audio threads
    std::atomic<float> volume{1.0f};
    uint32_t rng = 0x9E3779B9u; // variant/pitch-variation LCG (game thread only)

    float rand01() {
        rng = rng * 1664525u + 1013904223u;
        return float(rng >> 8) / float(1u << 24); // [0,1)
    }

    void playFrom(int bank, float gain, float pitch) {
        if (!deviceOk) return;
        const auto& variants = banks[bank];
        if (variants.empty()) return;
        size_t pick = size_t(rand01() * variants.size()) % variants.size();
        std::lock_guard<std::mutex> lock(voiceMutex);
        for (Voice& v : voices) {
            if (v.active) continue;
            v.buf = &variants[pick];
            v.pos = 0.0f;
            v.step = pitch;
            v.gain = gain;
            v.active = true;
            return;
        } // all voices busy: drop the sound — inaudible in practice
    }

    static void callback(ma_device* dev, void* out, const void*, ma_uint32 frames) {
        auto* self = static_cast<Impl*>(dev->pUserData);
        float* dst = static_cast<float*>(out); // mono f32, pre-zeroed by miniaudio
        float vol = self->volume.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(self->voiceMutex);
        for (Voice& v : self->voices) {
            if (!v.active) continue;
            const std::vector<float>& s = *v.buf;
            for (ma_uint32 i = 0; i < frames; ++i) {
                size_t idx = size_t(v.pos);
                if (idx + 1 >= s.size()) { v.active = false; break; }
                // Linear interpolation: stepping by a fractional pitch
                // without it aliases audibly.
                float frac = v.pos - float(idx);
                float smp = s[idx] + (s[idx + 1] - s[idx]) * frac;
                dst[i] += smp * v.gain * vol;
                v.pos += v.step;
            }
        }
        // Soft limiter: overlapping voices may sum past +/-1, and hard
        // digital clipping is exactly the kind of harshness we synthesized
        // so carefully to avoid.
        for (ma_uint32 i = 0; i < frames; ++i)
            dst[i] = std::tanh(dst[i]);
    }
};

Audio::~Audio() {
    if (!impl_) return;
    if (impl_->deviceOk) ma_device_uninit(&impl_->device);
    delete impl_;
}

bool Audio::init() {
    impl_ = new Impl();
    for (int b = 0; b < SOUND_BANK_COUNT; ++b) impl_->banks[b] = soundVariants(b);

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 1;
    cfg.sampleRate = SOUND_RATE; // the embedded data rate; the OS resamples
    cfg.dataCallback = Impl::callback;
    cfg.pUserData = impl_;
    if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) return false;
    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        return false;
    }
    impl_->deviceOk = true;
    return true;
}

void Audio::playBreak(SoundMat m) {
    if (!impl_ || m == SoundMat::None) return;
    impl_->playFrom(int(m) - 1, 1.0f, 0.95f + 0.1f * impl_->rand01());
}

void Audio::playPlace(SoundMat m) {
    if (!impl_ || m == SoundMat::None) return;
    // Same family as breaking, knocked up a fourth and quieter — reads as
    // "set down" rather than "smash" without needing more recordings.
    impl_->playFrom(int(m) - 1, 0.65f, 1.3f + 0.1f * impl_->rand01());
}

void Audio::playFootstep() {
    if (!impl_) return;
    impl_->playFrom(SOUND_BANK_STEP, 0.35f, 0.95f + 0.1f * impl_->rand01());
}

void Audio::setVolume(float v) {
    if (!impl_) return;
    impl_->volume.store(v < 0 ? 0.0f : (v > 1 ? 1.0f : v), std::memory_order_relaxed);
}

#else // !AUDIO_ENABLED — silent stubs so call sites need no #ifdefs

Audio::~Audio() {}
bool Audio::init() { return false; }
void Audio::playBreak(SoundMat) {}
void Audio::playPlace(SoundMat) {}
void Audio::playFootstep() {}
void Audio::setVolume(float) {}

#endif
