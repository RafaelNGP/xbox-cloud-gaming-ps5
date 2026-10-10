// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "media/audio_encoder.h"

#include "util/log.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <cstring>
#include <string>

namespace xc::media {

namespace {

std::string averr(int rc) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(rc, buf, sizeof buf);
    return buf;
}

}  // namespace

AudioEncoder::AudioEncoder() = default;

AudioEncoder::~AudioEncoder() {
    if (ctx_) avcodec_free_context(&ctx_);
    if (frame_) av_frame_free(&frame_);
    if (packet_) av_packet_free(&packet_);
}

bool AudioEncoder::init(int bitrate) {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
    if (!codec) {
        XC_LOGE("audio encoder: Opus encoder not found in FFmpeg build");
        return false;
    }

    ctx_ = avcodec_alloc_context3(codec);
    if (!ctx_) return false;

    ctx_->sample_rate = kSampleRate;
    ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_default(&ctx_->ch_layout, kChannels);
    ctx_->bit_rate = bitrate;
    ctx_->time_base = AVRational{1, kSampleRate};
    ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;

    ctx_->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    av_opt_set_double(ctx_->priv_data, "opus_delay", 20.0, 0);

    int rc = avcodec_open2(ctx_, codec, nullptr);
    if (rc < 0) {
        XC_LOGE("audio encoder: avcodec_open2 failed: %s", averr(rc).c_str());
        avcodec_free_context(&ctx_);
        return false;
    }

    constexpr int kSubframeSize = 120;  // Native Opus encoder frame_size is 120 (2.5 ms)
    frame_ = av_frame_alloc();
    if (!frame_) return false;
    frame_->nb_samples = kSubframeSize;
    frame_->format = ctx_->sample_fmt;
    frame_->sample_rate = ctx_->sample_rate;
    av_channel_layout_copy(&frame_->ch_layout, &ctx_->ch_layout);

    rc = av_frame_get_buffer(frame_, 0);
    if (rc < 0) {
        XC_LOGE("audio encoder: av_frame_get_buffer failed: %s", averr(rc).c_str());
        return false;
    }

    packet_ = av_packet_alloc();
    if (!packet_) return false;

    XC_LOGI("audio encoder: Opus encoder initialized (%d Hz, %d ch, %d bps, 20ms frames)",
            kSampleRate, kChannels, bitrate);
    return true;
}

bool AudioEncoder::encode(const int16_t* pcm, size_t samples, std::vector<uint8_t>& out) {
    if (!ctx_ || !frame_ || !packet_ || !pcm || samples < kFrameSize) return false;

    constexpr int kSubframeSize = 120;
    constexpr float kInv32768 = 1.0f / 32768.0f;
    bool gotPacket = false;

    // Feed 8 subframes of 120 samples (8 * 120 = 960 samples = 20 ms)
    for (size_t offset = 0; offset < kFrameSize; offset += kSubframeSize) {
        int rc = av_frame_make_writable(frame_);
        if (rc < 0) return false;

        float* dst = reinterpret_cast<float*>(frame_->data[0]);
        for (int i = 0; i < kSubframeSize; ++i) {
            dst[i] = static_cast<float>(pcm[offset + i]) * kInv32768;
        }

        frame_->pts = pts_;
        pts_ += kSubframeSize;

        rc = avcodec_send_frame(ctx_, frame_);
        if (rc < 0) {
            XC_LOGW("audio encoder: avcodec_send_frame failed: %s", averr(rc).c_str());
            return false;
        }

        rc = avcodec_receive_packet(ctx_, packet_);
        if (rc == 0) {
            out.assign(packet_->data, packet_->data + packet_->size);
            av_packet_unref(packet_);
            gotPacket = true;
        }
    }

    return gotPacket;
}

}  // namespace xc::media
