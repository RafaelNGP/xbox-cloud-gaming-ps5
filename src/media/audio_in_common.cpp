// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "media/audio_in_common.h"

#include <algorithm>
#include <cmath>

namespace xc::media {

void MicPipeline::process(int16_t* pcm, size_t n, bool muted, float gain, std::atomic<float>& level,
                          const AudioIn::AudioCallback& cb) {
    float peak = 0.0f;
    if (muted) {
        std::fill(pcm, pcm + n, 0);
    } else {
        for (size_t i = 0; i < n; ++i) {
            int16_t& s = pcm[i];
            if (std::fabs(gain - 1.0f) > 0.01f) {
                float scaled = static_cast<float>(s) * gain;
                scaled = std::clamp(scaled, -32768.0f, 32767.0f);
                s = static_cast<int16_t>(scaled);
            }
            float a = std::abs(static_cast<float>(s)) / 32768.0f;
            if (a > peak) peak = a;
        }
    }

    // Perceptual scale: speech in DualSense typically has 500..6000 amplitude
    float scaledPeak = std::clamp(std::sqrt(peak * 3.5f), 0.0f, 1.0f);

    float prev = level.load(std::memory_order_relaxed);
    // Smooth natural VU decay (~350ms falloff)
    float nextLevel = (scaledPeak >= prev) ? scaledPeak : (prev * 0.96f);
    if (nextLevel < 0.01f) nextLevel = 0.0f;
    level.store(nextLevel, std::memory_order_relaxed);

    accumulated_.insert(accumulated_.end(), pcm, pcm + n);

    while (accumulated_.size() >= AudioIn::kFrameSize) {
        if (cb) {
            cb(accumulated_.data(), AudioIn::kFrameSize);
        }
        accumulated_.erase(accumulated_.begin(), accumulated_.begin() + static_cast<long>(AudioIn::kFrameSize));
    }
}

}  // namespace xc::media
