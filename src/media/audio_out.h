// Audio playback: 48 kHz interleaved float stereo pushed from any thread,
// played by a dedicated thread in 256-frame grains (silence on underrun).
#pragma once

#include <cstddef>

namespace xc::media {

bool audioStart();
void audioStop();
// Drops the oldest samples when more than ~120 ms is queued, to keep
// latency bounded when the network delivers a burst.
void audioPush(const float* interleaved, size_t frames);

}  // namespace xc::media
