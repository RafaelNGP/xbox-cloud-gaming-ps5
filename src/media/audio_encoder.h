// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace xc::media {

class AudioEncoder {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels = 1;
    static constexpr int kFrameSize = 960;  // 20 ms @ 48 kHz

    AudioEncoder();
    ~AudioEncoder();

    AudioEncoder(const AudioEncoder&) = delete;
    AudioEncoder& operator=(const AudioEncoder&) = delete;

    // Initializes native Opus encoder (default 24 kbps VoIP)
    bool init(int bitrate = 24000);

    // Encodes one 20 ms frame (960 mono S16 samples).
    // Fills `out` with the encoded Opus packet bytes.
    bool encode(const int16_t* pcm, size_t samples, std::vector<uint8_t>& out);

private:
    AVCodecContext* ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    int64_t pts_ = 0;
};

}  // namespace xc::media
