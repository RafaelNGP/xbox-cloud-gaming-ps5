// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "media/audio_out.h"
#include "media/audio_queue.h"
#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
int sceAudioOutInit(void);
int sceAudioOutOpen(int userId, int type, int index, unsigned len, unsigned freq, unsigned param);
int sceAudioOutOutput(int handle, const void* ptr);
int sceAudioOutClose(int handle);
int sceAudioOutSetMixLevelPadSpk(int handle, int mixLevel);
int sceAudioOutSetPadSpkVolume(int handle, int volume);
int sceSysmoduleLoadModuleInternal(uint32_t id);
}

namespace xc::media {

namespace {

constexpr int kRate = AudioQueue::kRate;
constexpr unsigned kGrain = 256;

std::atomic<bool> g_running{false};
std::thread g_thread;
int g_handle = -1;
std::atomic<int> g_padSpeakerVolume{100};

void loop() {
    platform::setThreadAffinity(platform::ThreadRole::Audio, "ps4_audio_out");
    platform::setThreadRealtimePriority();
    std::vector<float> grain(kGrain * 2);
    AudioQueue& queue = audioQueue();
    while (g_running) {
        queue.pull(grain.data(), kGrain);
        int h = g_handle;
        if (h < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // Float 32-bit interleaved stereo output (format 4)
        int rc = sceAudioOutOutput(h, grain.data());
        if (rc < 0) {
            static uint64_t failures = 0;
            if (failures++ % 200 == 0) {
                XC_LOGW("sceAudioOutOutput: 0x%08x (failure %llu)", static_cast<unsigned>(rc),
                        static_cast<unsigned long long>(failures));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (!g_running) break;
        }
    }
    int h = g_handle;
    if (h >= 0) {
        sceAudioOutOutput(h, nullptr);
    }
}

}  // namespace

void setPadSpeakerVolume(int percent) {
    g_padSpeakerVolume = std::clamp(percent, 0, 100);
}

int padSpeakerVolume() {
    return g_padSpeakerVolume.load();
}

bool audioStart() {
    if (g_running) return true;
    static bool initialised = false;
    if (!initialised) {
        sceSysmoduleLoadModuleInternal(0x80000001); // AUDIOOUT
        int rc = sceAudioOutInit();
        if (rc < 0 && rc != static_cast<int>(0x8026000C) && rc != static_cast<int>(0x8026000E)) {
            XC_LOGE("sceAudioOutInit: 0x%08x", static_cast<unsigned>(rc));
            return false;
        }
        initialised = true;
    }

    // Type 0 = Main port, format 4 = Float stereo (SCE_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO)
    g_handle = sceAudioOutOpen(0xFF, 0, 0, kGrain, kRate, 4);
    if (g_handle < 0) {
        XC_LOGE("sceAudioOutOpen: 0x%08x", static_cast<unsigned>(g_handle));
        return false;
    }
    g_running = true;
    audioQueue().open();
    g_thread = std::thread(loop);
    XC_LOGI("audio output open (PS4 48 kHz float stereo, handle %d)", g_handle);
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

bool playPadSpeakerAudio(const float*, size_t) {
    return false;
}

bool hasPadSpeaker() {
    return false;
}

}  // namespace xc::media
