// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Audio playback: 48 kHz interleaved float stereo pushed from any thread,
// played by a dedicated thread in 256-frame grains (silence on underrun).
#pragma once

#include <cstddef>

namespace xc::media {

enum class AudioRoute {
    Main = 0,  // TV / HDMI / Default output
    Pad = 1    // DualSense Controller Speaker / Headset
};

bool audioStart();
void audioStop();
void audioSetRoute(AudioRoute route);
AudioRoute audioRoute();
// Drops the oldest samples when more than ~120 ms is queued, to keep
// latency bounded when the network delivers a burst.
void audioPush(const float* interleaved, size_t frames);

}  // namespace xc::media
