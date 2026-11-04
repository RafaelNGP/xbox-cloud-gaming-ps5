// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "media/audio_in.h"
#include "input/controller.h"
#include "media/audio_in_common.h"
#include "platform/platform.h"
#include "util/log.h"

#include <thread>
#include <vector>

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
int sceSysmoduleLoadModuleInternal(uint32_t id);
}

namespace xc::media {

struct AudioIn::Impl {
    platform::Thread thread;
    int handle = -1;
    MicPipeline mic;
};

AudioIn::AudioIn() : impl_(std::make_unique<Impl>()) {}

AudioIn::~AudioIn() {
    close();
}

bool AudioIn::open(AudioCallback cb) {
    if (running_) return true;
    callback_ = std::move(cb);

    sceSysmoduleLoadModuleInternal(0x80000002); // AUDIOIN

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
            XC_LOGW("audio in: no signed-in user, trying the system user");
            userId = 0xFF; // System user on PS4
        }
    }
    XC_LOGI("audio in: opening port for user 0x%08x (%d)...", static_cast<unsigned>(userId), userId);

    constexpr unsigned kGrain = MicPipeline::kGrain; // 256
    // The PS4 refuses 48 kHz on its voice and personal ports (0x80260103 in #301 and #309):
    // its microphone runs at 16 kHz. 48 kHz is still tried first (it is what the stream
    // sends), then 16 kHz, whose grains are upsampled 3x below.
    unsigned openedRate = 0;
    const int users[] = {userId, 0xFF};
    for (unsigned rate : {static_cast<unsigned>(kSampleRate), 16000u}) {
        for (int u = 0; u < 2; ++u) {
            const int user = users[u];
            if (u == 1 && userId == 0xFF) continue;  // already tried as the first user
            for (int type : {0, 1}) {  // 0 = voice chat port, 1 = personal port
                int h = sceAudioInOpen(user, type, 0, kGrain, rate, 0);  // mono S16
                if (h >= 0) {
                    impl_->handle = h;
                    openedRate = rate;
                    XC_LOGI("audio in: port type %d opened for user 0x%08x at %u Hz", type,
                            static_cast<unsigned>(user), rate);
                    break;
                }
                XC_LOGW("sceAudioInOpen(user 0x%x, type %d, %u Hz): 0x%08x", static_cast<unsigned>(user), type,
                        rate, static_cast<unsigned>(h));
            }
            if (openedRate) break;
        }
        if (openedRate) break;
    }
    if (!openedRate) {
        impl_->handle = -1;
        XC_LOGE("sceAudioInOpen failed on every port and rate");
        return false;
    }
    running_ = true;
    impl_->mic.setFrameSize(frameSize_);

    platform::startThread(impl_->thread, [this, openedRate]() {
        platform::setThreadAffinity(platform::ThreadRole::Audio, "ps4_audio_in");
        platform::setThreadRealtimePriority();
        std::vector<int16_t> grain(kGrain);
        const unsigned factor = kSampleRate / openedRate;  // 1, or 3 at 16 kHz
        std::vector<int16_t> upsampled(kGrain * factor);
        int16_t lastSample = 0;
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
            if (factor == 1) {
                impl_->mic.process(grain.data(), grain.size(), isMuted, gain_.load(std::memory_order_relaxed), level_, callback_);
            } else {
                // Linear interpolation from the previous sample to each new one.
                for (unsigned i = 0; i < kGrain; ++i) {
                    int a = lastSample, b = grain[i];
                    for (unsigned k = 0; k < factor; ++k) {
                        upsampled[i * factor + k] = static_cast<int16_t>(a + (b - a) * static_cast<int>(k + 1) / static_cast<int>(factor));
                    }
                    lastSample = grain[i];
                }
                impl_->mic.process(upsampled.data(), upsampled.size(), isMuted, gain_.load(std::memory_order_relaxed), level_, callback_);
            }
        }
    }, 512u << 10);

    XC_LOGI("audio in: PS4 microphone open (handle %d, %u Hz mono S16%s)", impl_->handle, openedRate,
            openedRate == static_cast<unsigned>(kSampleRate) ? "" : ", upsampled to 48 kHz");
    return true;
}

void AudioIn::close() {
    if (!running_) return;
    running_ = false;
    level_.store(0.0f, std::memory_order_relaxed);
    hardwareMuted_.store(false, std::memory_order_relaxed);

    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }

    if (impl_->handle >= 0) {
        sceAudioInClose(impl_->handle);
        impl_->handle = -1;
    }
    callback_ = nullptr;
    XC_LOGI("audio in: closed");
}

} // namespace xc::media
