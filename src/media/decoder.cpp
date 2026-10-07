// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PSBox Cloud Gaming contributors
#include "media/decoder.h"

#include "util/log.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/cpu.h>
#include <libavutil/log.h>
#include <libavutil/error.h>
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
    ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    ctx_->flags2 |= AV_CODEC_FLAG2_FAST;
    ctx_->thread_count = std::max(1, threads);
    ctx_->thread_type = FF_THREAD_SLICE;
    XC_LOGI("h264 decoder: cpu flags 0x%x", av_get_cpu_flags());
    int rc = avcodec_open2(ctx_, codec, nullptr);
    if (rc < 0) {
        XC_LOGE("avcodec_open2(h264): %s", averr(rc).c_str());
        return false;
    }
    return true;
}

bool VideoDecoder::decode(const uint8_t* data, size_t len, Picture& out) {
    // An empty packet means "end of stream" to libavcodec and would switch it
    // to draining for good; the RTP depacketizer emits them after packet loss.
    if (!ctx_ || len == 0) return false;
    packet_->data = const_cast<uint8_t*>(data);
    packet_->size = static_cast<int>(len);
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
    if (rc < 0) {
        if (rc != AVERROR(EAGAIN)) needsKeyframe_ = true;
        if (failuresLogged_++ < 20)
            XC_LOGI("h264 no picture: send=%d receive=%s (%zu bytes, first NAL type %d)", sent, averr(rc).c_str(),
                    len, len > 4 ? data[4] & 0x1f : -1);
        return false;
    }
    if (frame_->decode_error_flags || (frame_->flags & AV_FRAME_FLAG_CORRUPT)) {
        needsKeyframe_ = true;
    } else if (frame_->flags & AV_FRAME_FLAG_KEY) {
        needsKeyframe_ = false;
    }
    if (frame_->format != AV_PIX_FMT_YUV420P && frame_->format != AV_PIX_FMT_YUVJ420P) {
        XC_LOGW("unexpected pixel format %d", frame_->format);
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

bool AudioDecoder::decode(const uint8_t* data, size_t len, std::vector<float>& out) {
    if (!ctx_) return false;
    packet_->data = const_cast<uint8_t*>(data);
    packet_->size = static_cast<int>(len);
    if (avcodec_send_packet(ctx_, packet_) < 0) return false;
    bool any = false;
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
    return any;
}

}  // namespace xc::media
