// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// What every microphone backend (audio_in_ps5.cpp, audio_in_sdl.cpp) does
// with the samples it reads: the gain, muting, the level meter and the
// 20 ms frames the Opus encoder takes.
#pragma once

#include "media/audio_in.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace xc::media {

class MicPipeline {
public:
    MicPipeline() { accumulated_.reserve(AudioIn::kFrameSize + kGrain); }
    // The grain the PS5 reads (5.3 ms); the desktop hands its samples over
    // in the same steps, so the meter falls back as fast.
    static constexpr size_t kGrain = 256;

    // One grain of `n` mono S16 samples, changed in place (the gain, or
    // silence while muted): `level` (the meter, 0..1) follows its peak and
    // each full 20 ms frame goes to `cb`.
    void process(int16_t* pcm, size_t n, bool muted, float gain, std::atomic<float>& level,
                 const AudioIn::AudioCallback& cb);
    void reset() { accumulated_.clear(); }

private:
    std::vector<int16_t> accumulated_;
};

}  // namespace xc::media
