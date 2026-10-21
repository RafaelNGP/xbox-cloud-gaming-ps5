// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// No sound device (xcloud-tests, psbox-headless): the sound pushed is queued
// as on the PS5 (the queue keeps itself to ~60 ms) and never played.
#include "media/audio_out.h"

#include "media/audio_queue.h"

#include <atomic>

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

bool playPadSpeakerAudio(const float* interleavedStereo, size_t frames) {
    if (!interleavedStereo || frames == 0) return false;
    audioQueue().push(interleavedStereo, frames);
    return true;
}

}  // namespace xc::media
