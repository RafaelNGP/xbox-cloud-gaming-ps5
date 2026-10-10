// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace xc::media {

class AudioIn {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels = 1;
    static constexpr size_t kFrameSize = 960;  // 20 ms @ 48 kHz

    using AudioCallback = std::function<void(const int16_t* pcm, size_t frames)>;

    AudioIn();
    ~AudioIn();

    AudioIn(const AudioIn&) = delete;
    AudioIn& operator=(const AudioIn&) = delete;

    bool open(AudioCallback cb);
    void close();

    bool isRecording() const { return running_; }
    void setMuted(bool mute) { muted_ = mute; }
    bool isMuted() const { return muted_; }
    bool isHardwareMuted() const { return hardwareMuted_.load(std::memory_order_relaxed); }
    void setGain(float gain) { gain_ = gain; }
    float gain() const { return gain_; }
    float level() const { return level_.load(std::memory_order_relaxed); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
    std::atomic<bool> muted_{false};
    std::atomic<bool> hardwareMuted_{false};
    std::atomic<float> gain_{1.0f};
    std::atomic<float> level_{0.0f};
    AudioCallback callback_;
};

}  // namespace xc::media
