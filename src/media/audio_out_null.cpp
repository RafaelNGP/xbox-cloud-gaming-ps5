// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// No sound device (xcloud-tests, psbox-headless): the sound pushed is queued
// as on the PS5 (the queue keeps itself to ~60 ms) and never played.
#include "media/audio_out.h"

#include "media/audio_queue.h"

#include <algorithm>
#include <atomic>
#include <vector>

namespace xc::media {

namespace {
std::atomic<bool> g_running{false};
}  // namespace

bool audioStart() {
    if (g_running) return true;
    g_running = true;
    audioQueue().open();
    return true;
}

void audioStop() {
    if (!g_running) return;
    g_running = false;
    audioQueue().close();
    audioQueue().clear();
}

static std::atomic<int> g_padSpeakerVolume{100};

void setPadSpeakerVolume(int percent) {
    g_padSpeakerVolume = std::clamp(percent, 0, 100);
}

int padSpeakerVolume() {
    return g_padSpeakerVolume.load();
}

bool playPadSpeakerAudio(const float* interleavedStereo, size_t frames) {
    if (!interleavedStereo || frames == 0) return false;
    int vol = g_padSpeakerVolume.load();
    if (vol <= 0) return true;
    float gain = vol / 100.0f;
    std::vector<float> scaled(frames * 2);
    for (size_t i = 0; i < frames * 2; ++i) scaled[i] = interleavedStereo[i] * gain;
    audioQueue().push(scaled.data(), frames);
    return true;
}

}  // namespace xc::media
