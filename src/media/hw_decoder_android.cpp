// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// H.264 on Android via NDK AMediaCodec.
#include "media/hw_decoder.h"
#include "util/log.h"

#include <cstring>
#include <vector>

#if defined(__ANDROID__)
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#endif

namespace xc::media {

HwDecoder::~HwDecoder() {
#if defined(__ANDROID__)
    if (decoder_) {
        auto* codec = static_cast<AMediaCodec*>(decoder_);
        AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);
        decoder_ = nullptr;
    }
#endif
}

bool HwDecoder::init(int maxWidth, int maxHeight) {
#if defined(__ANDROID__)
    AMediaCodec* codec = AMediaCodec_createDecoderByType("video/avc");
    if (!codec) {
        XC_LOGW("hw_decoder_android: AMediaCodec_createDecoderByType failed");
        return false;
    }

    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, maxWidth > 0 ? maxWidth : 1920);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, maxHeight > 0 ? maxHeight : 1080);

    media_status_t status = AMediaCodec_configure(codec, format, nullptr, nullptr, 0);
    AMediaFormat_delete(format);

    if (status != AMEDIA_OK) {
        XC_LOGW("hw_decoder_android: AMediaCodec_configure failed: %d", status);
        AMediaCodec_delete(codec);
        return false;
    }

    status = AMediaCodec_start(codec);
    if (status != AMEDIA_OK) {
        XC_LOGW("hw_decoder_android: AMediaCodec_start failed: %d", status);
        AMediaCodec_delete(codec);
        return false;
    }

    decoder_ = codec;
    XC_LOGI("hw_decoder_android: initialized AMediaCodec video/avc (%dx%d)", maxWidth, maxHeight);
    return true;
#else
    (void)maxWidth;
    (void)maxHeight;
    return false;
#endif
}

bool HwDecoder::decode(const uint8_t* data, size_t len, uint64_t pts, Picture& out) {
#if defined(__ANDROID__)
    if (!decoder_ || !data || len == 0) return false;
    auto* codec = static_cast<AMediaCodec*>(decoder_);

    ssize_t inIdx = AMediaCodec_dequeueInputBuffer(codec, 2000);  // 2ms timeout
    if (inIdx >= 0) {
        size_t bufSize = 0;
        uint8_t* buf = AMediaCodec_getInputBuffer(codec, static_cast<size_t>(inIdx), &bufSize);
        if (buf && bufSize >= len) {
            std::memcpy(buf, data, len);
            AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(inIdx), 0, len, pts, 0);
        } else {
            AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(inIdx), 0, 0, pts, 0);
        }
    }

    AMediaCodecBufferInfo info{};
    ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, 2000);  // 2ms timeout
    if (outIdx >= 0) {
        size_t outSize = 0;
        uint8_t* outBuf = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(outIdx), &outSize);
        if (outBuf && info.size > 0) {
            static thread_local std::vector<uint8_t> s_androidFrameBuffer;
            s_androidFrameBuffer.assign(outBuf + info.offset, outBuf + info.offset + info.size);
            out.data = s_androidFrameBuffer.data();
            out.size = s_androidFrameBuffer.size();

            AMediaFormat* outFormat = AMediaCodec_getOutputFormat(codec);
            int32_t width = 0, height = 0, stride = 0;
            if (outFormat) {
                AMediaFormat_getInt32(outFormat, AMEDIAFORMAT_KEY_WIDTH, &width);
                AMediaFormat_getInt32(outFormat, AMEDIAFORMAT_KEY_HEIGHT, &height);
                AMediaFormat_getInt32(outFormat, AMEDIAFORMAT_KEY_STRIDE, &stride);
                AMediaFormat_delete(outFormat);
            }
            out.width = width > 0 ? static_cast<uint32_t>(width) : 1920;
            out.height = height > 0 ? static_cast<uint32_t>(height) : 1080;
            out.pitch = stride > 0 ? static_cast<uint32_t>(stride) : out.width;
            out.pitchBytes = out.pitch;
            out.format = 1;  // NV12
            out.pts = static_cast<uint64_t>(info.presentationTimeUs);
            out.error = false;
        }
        AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(outIdx), false);
        return true;
    }
    return false;
#else
    (void)data;
    (void)len;
    (void)pts;
    (void)out;
    return false;
#endif
}

}  // namespace xc::media
