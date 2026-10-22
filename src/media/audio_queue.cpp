// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The audio queue (audio_queue.h) and the parts of audio_out.h every
// backend shares: pushing, the volume, the route.
#include "media/audio_queue.h"

#include "input/controller.h"
#include "media/audio_out.h"
#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <cmath>

namespace xc::media {

void AudioQueue::push(const float* interleaved, size_t frames) {
    if (!open_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.insert(queue_.end(), interleaved, interleaved + frames * 2);
    if (queue_.size() > kMaxQueued * 2) {
        size_t cut = queue_.size() - kTrimTo * 2;
        stats_.trimmed += cut / 2;
        queue_.erase(queue_.begin(), queue_.begin() + static_cast<long>(cut));
        // Soft ramp-in on the trimmed front (1 ms = 48 stereo frames) to eliminate DC step pops
        constexpr size_t kRamp = 48;
        size_t rampN = std::min(kRamp * 2, queue_.size());
        for (size_t i = 0; i < rampN; i += 2) {
            float f = static_cast<float>(i / 2) / static_cast<float>(kRamp);
            queue_[i] *= f;
            queue_[i + 1] *= f;
        }
    }
}

void AudioQueue::pull(float* out, size_t frames) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t depth = queue_.size() / 2;
        stats_.depthSum += depth;
        stats_.depthMax = std::max<uint64_t>(stats_.depthMax, depth);
        ++stats_.pulls;
        stats_.frames += frames;
        if (stats_.frames >= static_cast<uint64_t>(kRate) * 10) {  // ~10 s (the PS5's 1875 grains of 256)
            XC_LOGI("audio out: queue %.1f ms on average (max %.1f), %llu empty grains, %.0f ms cut",
                    stats_.depthSum * 1000.0 / kRate / stats_.pulls, stats_.depthMax * 1000.0 / kRate,
                    static_cast<unsigned long long>(stats_.empty), stats_.trimmed * 1000.0 / kRate);
            stats_ = {};
        }
        if (queue_.empty()) {
            ++stats_.empty;
            std::fill(out, out + frames * 2, 0.0f);
            return;
        }
        size_t n = std::min(frames * 2, queue_.size());
        std::copy(queue_.begin(), queue_.begin() + static_cast<long>(n), out);
        queue_.erase(queue_.begin(), queue_.begin() + static_cast<long>(n));
        if (n < frames * 2) {
            // Soft ramp-out on the tail of partial buffer to eliminate underrun pops
            constexpr size_t kFade = 48;
            if (n >= kFade * 2) {
                for (size_t i = 0; i < kFade * 2; i += 2) {
                    float f = 1.0f - static_cast<float>(i / 2) / static_cast<float>(kFade);
                    out[n - kFade * 2 + i] *= f;
                    out[n - kFade * 2 + i + 1] *= f;
                }
            }
            std::fill(out + n, out + frames * 2, 0.0f);
            ++stats_.empty;
        }
    }
    if (float v = volume_.load(); v != 1.0f)
        for (size_t i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i] * v, -1.0f, 1.0f);
}

void AudioQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
}

void AudioQueue::setVolume(float gain) { volume_ = std::clamp(gain, 0.0f, 2.0f); }

size_t AudioQueue::queuedFrames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size() / 2;
}

AudioQueue::Stats AudioQueue::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

AudioQueue& audioQueue() {
    static AudioQueue queue;
    return queue;
}

namespace {
// Kept for the backends that can pick an output (none yet: the PS5's
// main port follows the system's own routing, the desktop plays on the
// default device).
std::atomic<AudioRoute> g_route{AudioRoute::Main};
std::atomic<int> g_voiceRouting{0};
}  // namespace

void audioSetVolume(float gain) { audioQueue().setVolume(gain); }
void audioSetRoute(AudioRoute route) { g_route.store(route); }
AudioRoute audioRoute() { return g_route.load(); }
void audioSetVoiceRouting(int routing) { g_voiceRouting.store(std::clamp(routing, 0, 1)); }
int audioVoiceRouting() { return g_voiceRouting.load(); }
void audioPush(const float* interleaved, size_t frames) { audioQueue().push(interleaved, frames); }

bool playVoiceAudio(const float* interleavedStereo, size_t frames) {
    if (!interleavedStereo || frames == 0) return false;
    if (g_voiceRouting.load() == 0 && hasPadSpeaker()) {
        return playPadSpeakerAudio(interleavedStereo, frames);
    }
    audioPush(interleavedStereo, frames);
    return true;
}

static std::atomic<int> g_audioHapticsLevel{0};

void setAudioHapticsLevel(int level) {
    g_audioHapticsLevel.store(level, std::memory_order_relaxed);
    input::setAudioHapticsEnabled(level > 0);
}

int audioHapticsLevel() {
    return g_audioHapticsLevel.load(std::memory_order_relaxed);
}

AudioToHaptics& audioToHaptics() {
    static AudioToHaptics s_instance;
    return s_instance;
}

AudioToHaptics::AudioToHaptics() {
    init(48000.0f, 120.0f);
}

void AudioToHaptics::init(float sampleRate, float cutoffHz) {
    float w0 = 2.0f * 3.14159265358979323846f * cutoffHz / sampleRate;
    float cosw0 = std::cos(w0);
    float sinw0 = std::sin(w0);
    float alpha = sinw0 / (2.0f * 0.70710678f);

    float b0 = (1.0f - cosw0) * 0.5f;
    float b1 = 1.0f - cosw0;
    float b2 = (1.0f - cosw0) * 0.5f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cosw0;
    float a2 = 1.0f - alpha;

    b0_ = b0 / a0;
    b1_ = b1 / a0;
    b2_ = b2 / a0;
    a1_ = a1 / a0;
    a2_ = a2 / a0;

    decay_ = std::exp(-1.0f / (sampleRate * 0.100f));
    reset();
}

void AudioToHaptics::reset() {
    x1_ = x2_ = y1_ = y2_ = 0.0f;
    env_ = 0.0f;
    lastKickMs_ = 0;
}

float AudioToHaptics::processSample(float inMono) {
    float out = b0_ * inMono + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
    x2_ = x1_;
    x1_ = inMono;
    y2_ = y1_;
    y1_ = out;
    if (std::abs(y1_) < 1e-15f) y1_ = 0.0f;
    if (std::abs(y2_) < 1e-15f) y2_ = 0.0f;
    return out;
}

void AudioToHaptics::process(const float* interleavedStereo, size_t frames, int level) {
    if (level <= 0 || !interleavedStereo || frames == 0) return;

    for (size_t i = 0; i < frames; ++i) {
        float mono = 0.5f * (interleavedStereo[i * 2] + interleavedStereo[i * 2 + 1]);
        float low = processSample(mono);
        float e = low * low;
        if (e > env_) {
            env_ = e;
        } else {
            env_ *= decay_;
            if (env_ < 1e-9f) env_ = 0.0f;
        }
    }

    float thresh = 0.05f;
    float mult = 1500.0f;
    float minKick = 45.0f;
    float maxKick = 190.0f;
    if (level == 1) { // Baixo
        thresh = 0.08f;
        mult = 1200.0f;
        minKick = 35.0f;
        maxKick = 130.0f;
    } else if (level == 3) { // Alto
        thresh = 0.025f;
        mult = 1800.0f;
        minKick = 60.0f;
        maxKick = 255.0f;
    }

    uint64_t now = platform::nowMs();
    if (env_ > thresh && now >= lastKickMs_ + 55) {
        uint8_t kick = static_cast<uint8_t>(std::clamp((env_ - thresh) * mult, minKick, maxKick));
        input::setAudioHaptics(kick, 60);
        lastKickMs_ = now;
    }
}

}  // namespace xc::media
