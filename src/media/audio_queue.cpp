// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The audio queue (audio_queue.h) and the parts of audio_out.h every
// backend shares: pushing, the volume, the route.
#include "media/audio_queue.h"

#include "media/audio_out.h"
#include "util/log.h"

#include <algorithm>

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

}  // namespace xc::media
