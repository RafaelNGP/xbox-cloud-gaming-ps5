// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The game's sound on its way to the speakers, shared by every audio-out
// backend (audio_out_ps5.cpp, audio_out_sdl.cpp, audio_out_null.cpp):
// pushed as it is decoded, pulled by the device a grain at a time, silence
// when it runs dry. Past ~60 ms queued (a burst from the network) it is cut
// back to ~40 ms, the oldest samples first, to keep the sound close to the
// picture. Its depth, the pulls that found it empty and what was cut are
// logged every ~10 s of sound.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace xc::media {

class AudioQueue {
public:
    static constexpr int kRate = 48000;
    // Bounded to keep the sound close to the picture while absorbing normal
    // network jitter (20-40 ms bursts, up to 150 ms NACK recovery). Stereo frames.
    static constexpr size_t kMaxQueued = kRate * 250 / 1000;  // 250 ms
    static constexpr size_t kTrimTo = kRate * 140 / 1000;     // 140 ms

    // Since the last log (reset by it).
    struct Stats {
        uint64_t pulls = 0, empty = 0;  // pulls, those that found it empty
        uint64_t frames = 0;            // pulled
        uint64_t depthSum = 0, depthMax = 0, trimmed = 0;  // frames: queued at each pull, cut
    };

    // Interleaved stereo frames; ignored while closed.
    void push(const float* interleaved, size_t frames);
    // `frames` stereo frames into `out`: what is queued, silence past it, at
    // the volume (clipped at full scale).
    void pull(float* out, size_t frames);
    // A backend opens it as its device starts; closing stops the pushes
    // (clear() then empties it, once the device is done).
    void open() { open_ = true; }
    void close() { open_ = false; }
    bool isOpen() const { return open_; }
    void clear();
    // 1 as it comes, up to 2 (louder).
    void setVolume(float gain);
    float volume() const { return volume_; }
    size_t queuedFrames() const;
    Stats stats() const;

private:
    mutable std::mutex mutex_;
    std::deque<float> queue_;  // interleaved stereo
    std::atomic<bool> open_{false};
    std::atomic<float> volume_{1.0f};
    Stats stats_;  // under mutex_
};

// The one audio_out.h's audioPush() fills; the backend pulls from it and
// opens / closes it with its device.
AudioQueue& audioQueue();

class AudioToHaptics {
public:
    AudioToHaptics();
    void init(float sampleRate = 48000.0f, float cutoffHz = 120.0f);
    void reset();
    float processSample(float inMono);
    void process(const float* interleavedStereo, size_t frames, int level);
    float envelope() const { return env_; }

private:
    float b0_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;
    float x1_ = 0.0f, x2_ = 0.0f;
    float y1_ = 0.0f, y2_ = 0.0f;
    float env_ = 0.0f;
    float decay_ = 0.999f;
    uint64_t lastKickMs_ = 0;
};

void setAudioHapticsLevel(int level);
int audioHapticsLevel();
AudioToHaptics& audioToHaptics();

}  // namespace xc::media
