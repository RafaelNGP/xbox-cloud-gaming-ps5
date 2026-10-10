// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "media/audio_in.h"
#include "input/controller.h"
#include "util/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

#if defined(XCLOUD_PS5)
extern "C" {
struct SceUserServiceInitializeParams {
    uint32_t priority;
};
int sceUserServiceInitialize(const SceUserServiceInitializeParams* param);
int sceUserServiceGetInitialUser(int* userId);
int sceAudioInInit(void);
int sceAudioInOpen(int userId, int type, int index, unsigned len, unsigned freq, unsigned param);
int sceAudioInInput(int handle, void* dest);
int sceAudioInClose(int handle);
int sceAudioInGetSilentState(int handle);
int sceAudioInSetGain(int handle, int gain);
}
#endif

namespace xc::media {

struct AudioIn::Impl {
    std::thread thread;
#if defined(XCLOUD_PS5)
    int handle = -1;
#endif
};

AudioIn::AudioIn() : impl_(std::make_unique<Impl>()) {}

AudioIn::~AudioIn() {
    close();
}

bool AudioIn::open(AudioCallback cb) {
    if (running_) return true;
    callback_ = std::move(cb);

#if defined(XCLOUD_PS5)
    static bool initialised = false;
    if (!initialised) {
        int rc = sceAudioInInit();
        if (rc < 0 && rc != static_cast<int>(0x8026000C) && rc != static_cast<int>(0x8026000E)) {
            XC_LOGW("sceAudioInInit: 0x%08x", static_cast<unsigned>(rc));
        }
        initialised = true;
    }

    int userId = input::padUserId(0);
    if (userId < 0) {
        int rc = sceUserServiceGetInitialUser(&userId);
        if (rc < 0) {
            SceUserServiceInitializeParams param{0x2FF};
            sceUserServiceInitialize(&param);
            rc = sceUserServiceGetInitialUser(&userId);
        }
        if (rc < 0) {
            XC_LOGW("sceUserServiceGetInitialUser: 0x%08x", static_cast<unsigned>(rc));
        }
    }
    XC_LOGI("audio in: opening port for user 0x%08x (%d)...", static_cast<unsigned>(userId), userId);

    constexpr unsigned kGrain = 256;
    // type 0 = VoiceChat (or type 1 = General); param 0 = S16 mono
    impl_->handle = sceAudioInOpen(userId, 0, 0, kGrain, kSampleRate, 0);
    if (impl_->handle < 0) {
        XC_LOGW("sceAudioInOpen(user 0x%x, type 0) failed: 0x%08x, trying type 1...",
                (unsigned)userId, static_cast<unsigned>(impl_->handle));
        impl_->handle = sceAudioInOpen(userId, 1, 0, kGrain, kSampleRate, 0);
    }
    if (impl_->handle < 0 && userId != 0) {
        XC_LOGW("trying sceAudioInOpen with user 0...");
        impl_->handle = sceAudioInOpen(0, 0, 0, kGrain, kSampleRate, 0);
    }
    if (impl_->handle < 0) {
        XC_LOGE("sceAudioInOpen failed: 0x%08x", static_cast<unsigned>(impl_->handle));
        return false;
    }
    running_ = true;

    impl_->thread = std::thread([this]() {
        constexpr unsigned kGrain = 256;
        std::vector<int16_t> grain(kGrain);
        std::vector<int16_t> accumulated;
        accumulated.reserve(kFrameSize + kGrain);
        unsigned lastStatus = 0xFFFFFFFF;
        uint64_t loopCount = 0;

        while (running_) {
            int rc = sceAudioInInput(impl_->handle, grain.data());
            if (rc < 0) {
                XC_LOGE("sceAudioInInput error: 0x%08x", static_cast<unsigned>(rc));
                break;
            }
            if (++loopCount == 1) {
                XC_LOGI("audio in: first input buffer received (%u samples)", kGrain);
            }

            int silentState = sceAudioInGetSilentState(impl_->handle);
            if (silentState != static_cast<int>(lastStatus)) {
                XC_LOGI("audio in: silent state changed to 0x%08x", static_cast<unsigned>(silentState));
                lastStatus = static_cast<unsigned>(silentState);
            }
            if (silentState >= 0) {
                // ORBIS_AUDIO_IN_SILENT_STATE_USER_SETTING (0x4) or DEVICE_NONE (0x1)
                bool hwMuted = (silentState & 0x00000004) != 0 || (silentState & 0x00000001) != 0;
                hardwareMuted_.store(hwMuted, std::memory_order_relaxed);
            }

            bool isMuted = muted_.load(std::memory_order_relaxed) || hardwareMuted_.load(std::memory_order_relaxed);
            float currentGain = gain_.load(std::memory_order_relaxed);

            float peak = 0.0f;
            if (isMuted) {
                std::fill(grain.begin(), grain.end(), 0);
            } else {
                for (auto& s : grain) {
                    if (std::fabs(currentGain - 1.0f) > 0.01f) {
                        float scaled = static_cast<float>(s) * currentGain;
                        scaled = std::clamp(scaled, -32768.0f, 32767.0f);
                        s = static_cast<int16_t>(scaled);
                    }
                    float a = std::abs(static_cast<float>(s)) / 32768.0f;
                    if (a > peak) peak = a;
                }
            }

            // Perceptual scale: speech in DualSense typically has 500..6000 amplitude
            float scaledPeak = std::clamp(std::sqrt(peak * 3.5f), 0.0f, 1.0f);

            float prev = level_.load(std::memory_order_relaxed);
            // Smooth natural VU decay (~350ms falloff)
            float nextLevel = (scaledPeak >= prev) ? scaledPeak : (prev * 0.96f);
            if (nextLevel < 0.01f) nextLevel = 0.0f;
            level_.store(nextLevel, std::memory_order_relaxed);

            accumulated.insert(accumulated.end(), grain.begin(), grain.end());

            while (accumulated.size() >= kFrameSize) {
                if (callback_) {
                    callback_(accumulated.data(), kFrameSize);
                }
                accumulated.erase(accumulated.begin(), accumulated.begin() + static_cast<long>(kFrameSize));
            }
        }
    });

    XC_LOGI("audio in: PS5 audio input opened (handle %d, 48 kHz mono S16)", impl_->handle);
    return true;
#else
    running_ = true;
    XC_LOGI("audio in: host audio input stub started");
    return true;
#endif
}

void AudioIn::close() {
    if (!running_) return;
    running_ = false;
    level_.store(0.0f, std::memory_order_relaxed);
    hardwareMuted_.store(false, std::memory_order_relaxed);

    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }

#if defined(XCLOUD_PS5)
    if (impl_->handle >= 0) {
        sceAudioInClose(impl_->handle);
        impl_->handle = -1;
    }
#endif
    callback_ = nullptr;
    XC_LOGI("audio in: closed");
}

}  // namespace xc::media
