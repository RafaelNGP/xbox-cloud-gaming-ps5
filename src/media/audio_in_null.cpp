// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// No microphone (xcloud-tests, psbox-headless): "recording" with nothing
// coming, so the stream's voice chat path still opens and closes.
#include "media/audio_in.h"
#include "util/log.h"

namespace xc::media {

struct AudioIn::Impl {};

AudioIn::AudioIn() : impl_(std::make_unique<Impl>()) {}

AudioIn::~AudioIn() {
    close();
}

bool AudioIn::open(AudioCallback cb) {
    if (running_) return true;
    callback_ = std::move(cb);
    running_ = true;
    XC_LOGI("audio in: host audio input stub started");
    return true;
}

void AudioIn::close() {
    if (!running_) return;
    running_ = false;
    level_.store(0.0f, std::memory_order_relaxed);
    hardwareMuted_.store(false, std::memory_order_relaxed);
    callback_ = nullptr;
    XC_LOGI("audio in: closed");
}

}  // namespace xc::media
