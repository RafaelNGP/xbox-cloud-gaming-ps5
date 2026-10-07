// H.264 and Opus decoding through libavcodec (deps/ffmpeg, decoders only).
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace xc::media {

// A decoded picture in planar YUV 4:2:0; the planes stay valid until the
// next call to VideoDecoder::decode.
struct Picture {
    const uint8_t* y = nullptr;
    const uint8_t* u = nullptr;
    const uint8_t* v = nullptr;
    int strideY = 0, strideU = 0, strideV = 0;
    int width = 0, height = 0;
};

// Splits an Annex-B H.264 stream into access units (offset, length): at AUD
// or SPS NALs and at first slices not preceded by parameter sets/SEI.
std::vector<std::pair<size_t, size_t>> splitAccessUnits(const uint8_t* data, size_t size);

// Restricts FFmpeg to plain C code paths (diagnostics); call before init().
void disableSimd();

class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // `threads` > 1 enables slice threading (frame threading would add a
    // frame of latency per thread).
    bool init(int threads = 1);
    // One Annex-B access unit in; true with `out` filled when a picture is
    // ready. A false return without a picture is normal at stream start.
    bool decode(const uint8_t* data, size_t len, Picture& out);
    // True after a decode error: the caller should ask for a key frame.
    bool needsKeyframe() const { return needsKeyframe_; }

private:
    AVCodecContext* ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    bool needsKeyframe_ = true;
    int failuresLogged_ = 0;
};

class AudioDecoder {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels = 2;

    AudioDecoder() = default;
    ~AudioDecoder();
    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    bool init();
    // One Opus packet in; appends interleaved float stereo samples.
    bool decode(const uint8_t* data, size_t len, std::vector<float>& out);

private:
    AVCodecContext* ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
};

}  // namespace xc::media
