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
    }
}

void AudioQueue::pull(float* out, size_t frames) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t depth = queue_.size() / 2;
        stats_.depthSum += depth;
        stats_.depthMax = std::max<uint64_t>(stats_.depthMax, depth);
        if (depth == 0) ++stats_.empty;
        ++stats_.pulls;
        stats_.frames += frames;
        if (stats_.frames >= static_cast<uint64_t>(kRate) * 10) {  // ~10 s (the PS5's 1875 grains of 256)
            XC_LOGI("audio out: queue %.1f ms on average (max %.1f), %llu empty grains, %.0f ms cut",
                    stats_.depthSum * 1000.0 / kRate / stats_.pulls, stats_.depthMax * 1000.0 / kRate,
                    static_cast<unsigned long long>(stats_.empty), stats_.trimmed * 1000.0 / kRate);
            stats_ = {};
        }
        size_t n = std::min(frames * 2, queue_.size());
        std::copy(queue_.begin(), queue_.begin() + static_cast<long>(n), out);
        queue_.erase(queue_.begin(), queue_.begin() + static_cast<long>(n));
        std::fill(out + n, out + frames * 2, 0.0f);
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
}  // namespace

void audioSetVolume(float gain) { audioQueue().setVolume(gain); }
void audioSetRoute(AudioRoute route) { g_route.store(route); }
AudioRoute audioRoute() { return g_route.load(); }
void audioPush(const float* interleaved, size_t frames) { audioQueue().push(interleaved, frames); }

}  // namespace xc::media
