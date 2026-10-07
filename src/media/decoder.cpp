#include "media/decoder.h"

#include "util/log.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}

#include <algorithm>
#include <string>

namespace xc::media {

namespace {

std::string averr(int rc) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(rc, buf, sizeof buf);
    return buf;
}

void freeAll(AVCodecContext*& ctx, AVFrame*& frame, AVPacket*& packet) {
    avcodec_free_context(&ctx);
    av_frame_free(&frame);
    av_packet_free(&packet);
}

}  // namespace

// --- Video --------------------------------------------------------------------

VideoDecoder::~VideoDecoder() { freeAll(ctx_, frame_, packet_); }

bool VideoDecoder::init(int threads) {
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
    int rc = avcodec_open2(ctx_, codec, nullptr);
    if (rc < 0) {
        XC_LOGE("avcodec_open2(h264): %s", averr(rc).c_str());
        return false;
    }
    return true;
}

bool VideoDecoder::decode(const uint8_t* data, size_t len, Picture& out) {
    if (!ctx_) return false;
    packet_->data = const_cast<uint8_t*>(data);
    packet_->size = static_cast<int>(len);
    int rc = avcodec_send_packet(ctx_, packet_);
    if (rc < 0 && rc != AVERROR(EAGAIN)) {
        needsKeyframe_ = true;
        XC_LOGD("h264 send: %s", averr(rc).c_str());
        return false;
    }
    rc = avcodec_receive_frame(ctx_, frame_);
    if (rc < 0) {
        if (rc != AVERROR(EAGAIN)) needsKeyframe_ = true;
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
