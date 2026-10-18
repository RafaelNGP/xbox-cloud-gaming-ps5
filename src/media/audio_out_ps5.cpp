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

}  // namespace

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

}  // namespace xc::media
