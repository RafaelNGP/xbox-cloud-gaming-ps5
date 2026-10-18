// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "media/decoder.h"

#include "util/log.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/cpu.h>
#include <libavutil/log.h>
#include <libavutil/error.h>
#include <libavutil/video_enc_params.h>
}

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>
#include <string>

namespace xc::media {

namespace {

std::string averr(int rc) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(rc, buf, sizeof buf);
    return buf;
}

// FFmpeg's default log callback probes the terminal (isatty, getenv) and
// writes to stderr, which the console's sandbox does not support: route its
// messages to our log instead.
void ffmpegLog(void*, int level, const char* fmt, va_list ap) {
    if (level > AV_LOG_WARNING) return;
    char line[512];
    std::vsnprintf(line, sizeof line, fmt, ap);
    size_t n = std::strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    // The first messages at info level (diagnostics), the rest at debug.
    static std::atomic<int> shown{0};
    if (shown++ < 40)
        XC_LOGI("ffmpeg: %s", line);
    else
        XC_LOGD("ffmpeg: %s", line);
}

void installLogCallback() {
    static std::once_flag once;
    std::call_once(once, [] { av_log_set_callback(ffmpegLog); });
}

void freeAll(AVCodecContext*& ctx, AVFrame*& frame, AVPacket*& packet) {
    avcodec_free_context(&ctx);
    av_frame_free(&frame);
    av_packet_free(&packet);
}

}  // namespace

std::vector<std::pair<size_t, size_t>> splitAccessUnits(const uint8_t* data, size_t size) {
    std::vector<std::pair<size_t, size_t>> aus;
    size_t start = SIZE_MAX;
    int prevType = -1;
    for (size_t i = 0; i + 4 < size; ++i) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            int type = data[i + 3] & 0x1f;
            size_t sc = (i > 0 && data[i - 1] == 0) ? i - 1 : i;
            bool boundary = type == 9 || (type == 7 && prevType != 9) ||
                            ((type == 1 || type == 5) && prevType != 7 && prevType != 8 && prevType != 9 &&
                             prevType != 6 && (data[i + 4] & 0x80));
            if (boundary) {
                if (start != SIZE_MAX) aus.emplace_back(start, sc - start);
                start = sc;
            }
            prevType = type;
            i += 3;
        }
    }
    if (start != SIZE_MAX) aus.emplace_back(start, size - start);
    return aus;
}

void disableSimd() {
    av_force_cpu_flags(0);
    XC_LOGW("FFmpeg SIMD disabled (cpu flags 0)");
}

// --- Video --------------------------------------------------------------------

VideoDecoder::~VideoDecoder() { freeAll(ctx_, frame_, packet_); }

bool VideoDecoder::init(int threads) {
    installLogCallback();
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        XC_LOGE("no H.264 decoder in this FFmpeg build");
        return false;
    }
    ctx_ = avcodec_alloc_context3(codec);
    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!ctx_ || !frame_ || !packet_) return false;
    ctx_->flags2 |= AV_CODEC_FLAG2_FAST;
    ctx_->thread_count = std::max(1, threads);
    if (threads > 1) {
        // xCloud sends one slice per picture, so slice threads would idle:
        // decode several pictures at once instead, one frame of latency per
        // extra thread. (LOW_DELAY would turn frame threading off.)
        ctx_->thread_type = FF_THREAD_FRAME;
    } else {
        ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
        ctx_->thread_type = FF_THREAD_SLICE;
    }
    XC_LOGI("h264 decoder: cpu flags 0x%x, %d thread(s)", av_get_cpu_flags(), ctx_->thread_count);
    // The quantiser of each picture, for the quality measure (lastQp()).
    ctx_->export_side_data |= AV_CODEC_EXPORT_DATA_VIDEO_ENC_PARAMS;
    int rc = avcodec_open2(ctx_, codec, nullptr);
    if (rc < 0) {
        XC_LOGE("avcodec_open2(h264): %s", averr(rc).c_str());
        return false;
    }
    return true;
}

bool VideoDecoder::decode(const uint8_t* data, size_t len, Picture& out, uint32_t tag) {
    // An empty packet means "end of stream" to libavcodec and would switch it
    // to draining for good; the RTP depacketizer emits them after packet loss.
    pending_ = false;
    if (!ctx_ || len == 0) return false;
    packet_->data = const_cast<uint8_t*>(data);
    packet_->size = static_cast<int>(len);
    packet_->pts = tag;
    int rc = avcodec_send_packet(ctx_, packet_);
    if (rc == AVERROR_EOF) {
        // Drained anyway: reset and retry once.
        avcodec_flush_buffers(ctx_);
        rc = avcodec_send_packet(ctx_, packet_);
    }
    if (rc < 0 && rc != AVERROR(EAGAIN)) {
        needsKeyframe_ = true;
        if (failuresLogged_++ < 20) XC_LOGI("h264 send failed: %s (%zu bytes)", averr(rc).c_str(), len);
        return false;
    }
    int sent = rc;
    rc = avcodec_receive_frame(ctx_, frame_);
    if (rc == AVERROR(EAGAIN)) {
        pending_ = true;
        return false;
    }
    if (rc < 0) {
        needsKeyframe_ = true;
        if (failuresLogged_++ < 20)
            XC_LOGI("h264 no picture: send=%d receive=%s (%zu bytes, first NAL type %d)", sent, averr(rc).c_str(),
                    len, len > 4 ? data[4] & 0x1f : -1);
        return false;
    }
    if (sent == AVERROR(EAGAIN)) {
        // The decoder was full and didn't take this packet: now that a
        // picture came out, hand it over again (it used to be lost, and the
        // pictures after it decoded against a missing reference).
        int again = avcodec_send_packet(ctx_, packet_);
        if (again < 0) {
            needsKeyframe_ = true;
            if (failuresLogged_++ < 20) XC_LOGI("h264 packet dropped: %s (%zu bytes)", averr(again).c_str(), len);
        }
    }
    if (AVFrameSideData* sd = av_frame_get_side_data(frame_, AV_FRAME_DATA_VIDEO_ENC_PARAMS)) {
        auto* par = reinterpret_cast<AVVideoEncParams*>(sd->data);
        double sum = 0;
        for (unsigned i = 0; i < par->nb_blocks; ++i) sum += par->qp + av_video_enc_params_block(par, i)->delta_qp;
        lastQp_ = par->nb_blocks ? sum / par->nb_blocks : par->qp;
    }
    if (frame_->decode_error_flags || (frame_->flags & AV_FRAME_FLAG_CORRUPT)) {
        needsKeyframe_ = true;
    } else if (frame_->flags & AV_FRAME_FLAG_KEY) {
        needsKeyframe_ = false;
    }
    if (frame_->format != AV_PIX_FMT_YUV420P && frame_->format != AV_PIX_FMT_YUVJ420P) {
        if (failuresLogged_++ < 20) XC_LOGW("unexpected pixel format %d", frame_->format);
        return false;
    }
    out.y = frame_->data[0];
    out.u = frame_->data[1];
    out.v = frame_->data[2];
    out.strideY = frame_->linesize[0];
    out.strideU = frame_->linesize[1];
    out.strideV = frame_->linesize[2];
    out.width = frame_->width;
    out.height = frame_->height;
    if (!profileLogged_) {
        profileLogged_ = true;
        XC_LOGI("h264 stream: profile %d, level %d", ctx_->profile, ctx_->level);
    }
    out.tag = frame_->pts == AV_NOPTS_VALUE ? 0 : static_cast<uint32_t>(frame_->pts);
    return true;
}

// --- Audio --------------------------------------------------------------------

AudioDecoder::~AudioDecoder() { freeAll(ctx_, frame_, packet_); }

bool AudioDecoder::init() {
    installLogCallback();
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
    if (!codec) {
        XC_LOGE("no Opus decoder in this FFmpeg build");
        return false;
    }
    ctx_ = avcodec_alloc_context3(codec);
    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!ctx_ || !frame_ || !packet_) return false;
    ctx_->sample_rate = kSampleRate;
    av_channel_layout_default(&ctx_->ch_layout, kChannels);
    int rc = avcodec_open2(ctx_, codec, nullptr);
    if (rc < 0) {
        XC_LOGE("avcodec_open2(opus): %s", averr(rc).c_str());
        return false;
    }
    return true;
}

void AudioDecoder::resetPlc() {
    history_.clear();
    plcDecay_ = 1.0f;
    lastWasConcealed_ = false;
}

void AudioDecoder::conceal(std::vector<float>& out, size_t frames) {
    if (frames == 0) return;
    size_t base = out.size();
    out.resize(base + frames * 2);
    size_t histFrames = history_.size() / 2;
    if (histFrames == 0) {
        std::fill(out.begin() + static_cast<long>(base), out.end(), 0.0f);
        return;
    }
    // Repeat recent waveform period with smooth decaying attenuation
    size_t period = std::min(frames, histFrames);
    size_t histStart = (histFrames - period) * 2;
    for (size_t i = 0; i < frames; ++i) {
        size_t srcIdx = histStart + (i % period) * 2;
        float progress = static_cast<float>(i) / static_cast<float>(frames);
        float decay = plcDecay_ * (1.0f - 0.4f * progress);
        if (decay < 0.0f) decay = 0.0f;
        out[base + 2 * i] = history_[srcIdx] * decay;
        out[base + 2 * i + 1] = history_[srcIdx + 1] * decay;
    }
    plcDecay_ = std::max(0.0f, plcDecay_ * 0.6f);
    lastWasConcealed_ = true;
}

bool AudioDecoder::decode(const uint8_t* data, size_t len, std::vector<float>& out) {
    // An empty packet is "end of stream" to libavcodec: every packet after
    // it would be refused (no sound for the rest of the session).
    if (!ctx_ || !data || len == 0) return false;
    packet_->data = const_cast<uint8_t*>(data);
    packet_->size = static_cast<int>(len);
    int rc = avcodec_send_packet(ctx_, packet_);
    if (rc == AVERROR_EOF) {  // drained anyway: reset and retry once
        avcodec_flush_buffers(ctx_);
        rc = avcodec_send_packet(ctx_, packet_);
    }
    if (rc < 0) return false;
    bool any = false;
    size_t startSize = out.size();
    while (avcodec_receive_frame(ctx_, frame_) == 0) {
        int n = frame_->nb_samples;
        int ch = frame_->ch_layout.nb_channels;
        // FFmpeg's Opus decoder outputs planar float.
        if (frame_->format != AV_SAMPLE_FMT_FLTP || ch < 1) continue;
        const float* l = reinterpret_cast<const float*>(frame_->data[0]);
        const float* r = reinterpret_cast<const float*>(frame_->data[ch > 1 ? 1 : 0]);
        size_t base = out.size();
        out.resize(base + static_cast<size_t>(n) * 2);
        for (int i = 0; i < n; ++i) {
            out[base + 2 * i] = l[i];
            out[base + 2 * i + 1] = r[i];
        }
        any = true;
    }
    if (any) {
        size_t decodedFloats = out.size() - startSize;
        if (lastWasConcealed_) {
            // Smoothly ramp in newly decoded audio over the first ~2 ms (96 stereo frames)
            // to eliminate DC step pops after concealed frames.
            size_t rampFrames = std::min<size_t>(96, decodedFloats / 2);
            for (size_t i = 0; i < rampFrames; ++i) {
                float ramp = static_cast<float>(i) / static_cast<float>(rampFrames);
                out[startSize + 2 * i] *= ramp;
                out[startSize + 2 * i + 1] *= ramp;
            }
            lastWasConcealed_ = false;
        }
        plcDecay_ = 1.0f;
        // Keep up to 100 ms of history for PLC synthesis
        history_.insert(history_.end(), out.begin() + static_cast<long>(startSize), out.end());
        constexpr size_t kMaxHistoryFloats = kSampleRate * 2 * 100 / 1000;
        if (history_.size() > kMaxHistoryFloats) {
            history_.erase(history_.begin(), history_.begin() + static_cast<long>(history_.size() - kMaxHistoryFloats));
        }
    }
    return any;
}

}  // namespace xc::media
