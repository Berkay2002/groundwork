#include "Audio.h"

#ifdef AUDIO_ENABLED

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <atomic>
#include <mutex>
#include <vector>

#include "Sounds.h"

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
    std::vector<float> sounds[3]; // indexed by Sound
    Voice voices[MAX_VOICES];
    std::mutex voiceMutex; // guards voices between game and audio threads
    std::atomic<float> volume{1.0f};
    uint32_t rng = 0x9E3779B9u; // pitch-variation LCG (game thread only)

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
                if (idx >= s.size()) { v.active = false; break; }
                dst[i] += s[idx] * v.gain * vol;
                v.pos += v.step;
            }
        }
    }
};

Audio::~Audio() {
    if (!impl_) return;
    if (impl_->deviceOk) ma_device_uninit(&impl_->device);
    delete impl_;
}

bool Audio::init() {
    impl_ = new Impl();
    impl_->sounds[int(Sound::Break)] = makeBreakSound();
    impl_->sounds[int(Sound::Place)] = makePlaceSound();
    impl_->sounds[int(Sound::Footstep)] = makeFootstepSound();

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 1;
    cfg.sampleRate = SOUND_RATE;
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

void Audio::play(Sound s, float gain, float pitch) {
    if (!impl_ || !impl_->deviceOk) return;
    std::lock_guard<std::mutex> lock(impl_->voiceMutex);
    for (Voice& v : impl_->voices) {
        if (v.active) continue;
        v.buf = &impl_->sounds[int(s)];
        v.pos = 0.0f;
        v.step = pitch;
        v.gain = gain;
        v.active = true;
        return;
    } // all voices busy: drop the sound — inaudible in practice
}

void Audio::playVaried(Sound s, float gain) {
    if (!impl_) return;
    impl_->rng = impl_->rng * 1664525u + 1013904223u;
    float r = float(impl_->rng >> 8) / float(1u << 24); // [0,1)
    play(s, gain, 0.9f + 0.2f * r);
}

void Audio::setVolume(float v) {
    if (!impl_) return;
    impl_->volume.store(v < 0 ? 0.0f : (v > 1 ? 1.0f : v), std::memory_order_relaxed);
}

#else // !AUDIO_ENABLED — silent stubs so call sites need no #ifdefs

Audio::~Audio() {}
bool Audio::init() { return false; }
void Audio::play(Sound, float, float) {}
void Audio::playVaried(Sound, float) {}
void Audio::setVolume(float) {}

#endif
