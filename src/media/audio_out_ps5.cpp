// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Sound out on the PS5 (audio_out.h): libSceAudioOut's main port, fed by a
// thread of its own in 256-frame grains from the shared queue
// (audio_queue.h). audio_out_sdl.cpp and audio_out_null.cpp stand in
// elsewhere.
#include "media/audio_out.h"

#include "input/controller.h"
#include "media/audio_queue.h"
#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

extern "C" {
int sceAudioOutInit(void);
int sceAudioOutOpen(int userId, int type, int index, unsigned len, unsigned freq, unsigned param);
int sceAudioOutOutput(int handle, const void* ptr);
int sceAudioOutClose(int handle);
int sceAudioOutSetMixLevelPadSpk(int handle, int mixLevel);
int sceAudioOutSetPadSpkVolume(int handle, int volume);
}

namespace xc::media {

namespace {

constexpr int kRate = AudioQueue::kRate;
constexpr unsigned kGrain = 256;

std::atomic<bool> g_running{false};
std::thread g_thread;
int g_handle = -1;

void loop() {
    platform::setThreadAffinity(platform::ThreadRole::Audio, "ps5_audio_out");
    platform::setThreadRealtimePriority();
    std::vector<float> grain(kGrain * 2);
    AudioQueue& queue = audioQueue();
    float lowPass = 0.0f;
    while (g_running) {
        queue.pull(grain.data(), kGrain);  // silence past what is queued
        int h = g_handle;
        if (h < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // Audio-to-Haptics: detect deep sub-bass impacts (cutoff ~100 Hz at 48 kHz)
        if (input::audioHapticsEnabled()) {
            float energy = 0.0f;
            for (unsigned i = 0; i < kGrain; ++i) {
                float mono = 0.5f * (grain[i * 2] + grain[i * 2 + 1]);
                lowPass += 0.013f * (mono - lowPass);
                energy += lowPass * lowPass;
            }
            energy /= static_cast<float>(kGrain);
            if (energy > 0.06f) {
                uint8_t kick = static_cast<uint8_t>(std::clamp((energy - 0.06f) * 1400.0f, 40.0f, 180.0f));
                input::setAudioHaptics(kick, 60);
            }
        }

        // Blocks until the hardware wants the next grain (5.3 ms).
        int rc = sceAudioOutOutput(h, grain.data());
        if (rc < 0) {
            static uint64_t failures = 0;  // this thread only
            if (failures++ % 200 == 0)    // the first, then one a second or so
                XC_LOGW("sceAudioOutOutput: 0x%08x (failure %llu)", static_cast<unsigned>(rc),
                        static_cast<unsigned long long>(failures));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (!g_running) break;
        }
    }
}

std::atomic<int> g_padSpeakerVolume{100};

}  // namespace

void setPadSpeakerVolume(int percent) {
    g_padSpeakerVolume = std::clamp(percent, 0, 100);
}

int padSpeakerVolume() {
    return g_padSpeakerVolume.load();
}

bool audioStart() {
    if (g_running) return true;
    // Once per process: a second sceAudioOutInit answers "already
    // initialised" (0x8026000E on this firmware, 0x8026000C elsewhere).
    static bool initialised = false;
    if (!initialised) {
        int rc = sceAudioOutInit();
        if (rc < 0 && rc != static_cast<int>(0x8026000C) && rc != static_cast<int>(0x8026000E)) {
            XC_LOGE("sceAudioOutInit: 0x%08x", static_cast<unsigned>(rc));
            return false;
        }
        initialised = true;
    }
    // Port 0 (MAIN): primary system output. PS5 OS automatically routes
    // to HDMI/TV or connected headphones (DualSense 3.5mm / USB / Bluetooth).
    g_handle = sceAudioOutOpen(0xFF, 0, 0, kGrain, kRate, 4);
    if (g_handle < 0) {
        XC_LOGE("sceAudioOutOpen: 0x%08x", static_cast<unsigned>(g_handle));
        return false;
    }
    g_running = true;
    audioQueue().open();
    g_thread = std::thread(loop);
    XC_LOGI("audio output open (48 kHz float stereo, handle %d)", g_handle);
    return true;
}

void audioStop() {
    if (!g_running) return;
    g_running = false;
    audioQueue().close();
    if (g_thread.joinable()) g_thread.join();
    if (g_handle >= 0) {
        sceAudioOutClose(g_handle);
        g_handle = -1;
    }
    audioQueue().clear();
}

bool playPadSpeakerAudio(const float* interleavedStereo, size_t frames) {
    if (!interleavedStereo || frames == 0) return false;
    int volPercent = g_padSpeakerVolume.load();
    if (volPercent <= 0) return true;  // 0% volume: silenced

    std::thread([pcm = std::vector<float>(interleavedStereo, interleavedStereo + frames * 2), volPercent]() {
        platform::setThreadAffinity(platform::ThreadRole::Audio, "ps5_pad_audio");

        static bool initialised = false;
        if (!initialised) {
            int rc = sceAudioOutInit();
            if (rc >= 0 || rc == static_cast<int>(0x8026000C) || rc == static_cast<int>(0x8026000E)) {
                initialised = true;
            }
        }

        int32_t userId = input::padUserId(0);
        if (userId < 0) userId = 0xFF;

        int padHandle = -1;
        int activeFormat = -1;
        // Formats: 3 = FloatMono (native DualSense pad speaker), 4 = FloatStereo, 0 = S16Mono, 1 = S16Stereo
        const int formatsToTry[] = {3, 4, 0, 1};
        for (int fmt : formatsToTry) {
            padHandle = sceAudioOutOpen(userId, 4 /* PADSPK */, 0, kGrain, kRate, fmt);
            if (padHandle >= 0) {
                activeFormat = fmt;
                XC_LOGI("pad speaker: opened port handle 0x%08x for user 0x%08x (fmt %d)", padHandle, userId, fmt);
                break;
            } else {
                XC_LOGW("pad speaker: sceAudioOutOpen(user 0x%08x, fmt %d) returned 0x%08x", userId, fmt, static_cast<unsigned>(padHandle));
            }
        }

        if (padHandle < 0) {
            XC_LOGW("pad speaker: failed to open controller speaker port, fallback to main audio");
            audioStart();
            constexpr size_t kChunk = (kRate * 20) / 1000;
            float gain = volPercent / 100.0f;
            std::vector<float> scaledPcm(pcm.size());
            for (size_t i = 0; i < pcm.size(); ++i) scaledPcm[i] = pcm[i] * gain;
            size_t total = scaledPcm.size() / 2;
            size_t off = 0;
            while (off < total) {
                size_t n = std::min(kChunk, total - off);
                audioPush(scaledPcm.data() + off * 2, n);
                off += n;
                if (off < total) platform::sleepMs(18);
            }
            return;
        }

        int rawVol = (volPercent * 127 + 50) / 100;
        sceAudioOutSetMixLevelPadSpk(padHandle, 32768);
        sceAudioOutSetPadSpkVolume(padHandle, rawVol);

        size_t totalFrames = pcm.size() / 2;
        size_t offset = 0;

        if (activeFormat == 4) {
            std::vector<float> chunk(kGrain * 2, 0.0f);
            while (offset < totalFrames) {
                size_t n = std::min(static_cast<size_t>(kGrain), totalFrames - offset);
                std::fill(chunk.begin(), chunk.end(), 0.0f);
                std::copy(pcm.data() + offset * 2, pcm.data() + (offset + n) * 2, chunk.data());
                sceAudioOutOutput(padHandle, chunk.data());
                offset += n;
            }
        } else if (activeFormat == 3) {
            std::vector<float> chunk(kGrain, 0.0f);
            while (offset < totalFrames) {
                size_t n = std::min(static_cast<size_t>(kGrain), totalFrames - offset);
                std::fill(chunk.begin(), chunk.end(), 0.0f);
                for (size_t i = 0; i < n; ++i) {
                    chunk[i] = 0.5f * (pcm[(offset + i) * 2] + pcm[(offset + i) * 2 + 1]);
                }
                sceAudioOutOutput(padHandle, chunk.data());
                offset += n;
            }
        } else if (activeFormat == 0) {
            std::vector<int16_t> chunk(kGrain, 0);
            while (offset < totalFrames) {
                size_t n = std::min(static_cast<size_t>(kGrain), totalFrames - offset);
                std::fill(chunk.begin(), chunk.end(), 0);
                for (size_t i = 0; i < n; ++i) {
                    float m = 0.5f * (pcm[(offset + i) * 2] + pcm[(offset + i) * 2 + 1]);
                    chunk[i] = static_cast<int16_t>(std::clamp(m * 32767.0f, -32768.0f, 32767.0f));
                }
                sceAudioOutOutput(padHandle, chunk.data());
                offset += n;
            }
        } else if (activeFormat == 1) {
            std::vector<int16_t> chunk(kGrain * 2, 0);
            while (offset < totalFrames) {
                size_t n = std::min(static_cast<size_t>(kGrain), totalFrames - offset);
                std::fill(chunk.begin(), chunk.end(), 0);
                for (size_t i = 0; i < n * 2; ++i) {
                    chunk[i] = static_cast<int16_t>(std::clamp(pcm[offset * 2 + i] * 32767.0f, -32768.0f, 32767.0f));
                }
                sceAudioOutOutput(padHandle, chunk.data());
                offset += n;
            }
        }

        sceAudioOutClose(padHandle);
        XC_LOGI("pad speaker: alert chime playback completed successfully on DualSense");
    }).detach();

    return true;
}

bool hasPadSpeaker() {
    int32_t userId = input::padUserId(0);
    if (userId < 0) return false;
    static std::atomic<uint64_t> lastCheckMs{0};
    static std::atomic<bool> lastResult{false};
    uint64_t now = platform::nowMs();
    if (now - lastCheckMs.load() < 2000) {
        return lastResult.load();
    }
    lastCheckMs.store(now);
    int padHandle = sceAudioOutOpen(userId, 4 /* PADSPK */, 0, kGrain, kRate, 3);
    if (padHandle >= 0) {
        sceAudioOutClose(padHandle);
        lastResult.store(true);
        return true;
    }
    lastResult.store(false);
    return false;
}

}  // namespace xc::media
