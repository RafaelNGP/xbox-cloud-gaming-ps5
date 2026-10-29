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
}

namespace xc::media {

struct AudioIn::Impl {
    std::thread thread;
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
    // type 0 = VoiceChat; freq = 48000; param = 0 (mono S16)
    impl_->handle = sceAudioInOpen(userId, 0, 0, kGrain, kSampleRate, 0);
    if (impl_->handle < 0) {
        XC_LOGW("sceAudioInOpen(user 0x%x, voice): 0x%08x, trying the personal port",
                static_cast<unsigned>(userId), static_cast<unsigned>(impl_->handle));
        // type 1 = Personal port
        impl_->handle = sceAudioInOpen(userId, 1, 0, kGrain, kSampleRate, 0);
    }
    if (impl_->handle < 0 && userId != 0xFF) {
        XC_LOGW("trying sceAudioInOpen with system user 0xFF...");
        impl_->handle = sceAudioInOpen(0xFF, 0, 0, kGrain, kSampleRate, 0);
        if (impl_->handle < 0) {
            impl_->handle = sceAudioInOpen(0xFF, 1, 0, kGrain, kSampleRate, 0);
        }
    }
    if (impl_->handle < 0) {
        XC_LOGE("sceAudioInOpen failed: 0x%08x", static_cast<unsigned>(impl_->handle));
        return false;
    }
    running_ = true;
    impl_->mic.reset();

    impl_->thread = std::thread([this]() {
        platform::setThreadAffinity(platform::ThreadRole::Audio, "ps4_audio_in");
        platform::setThreadRealtimePriority();
        std::vector<int16_t> grain(kGrain);
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
            impl_->mic.process(grain.data(), grain.size(), isMuted, gain_.load(std::memory_order_relaxed), level_, callback_);
        }
    });

    XC_LOGI("audio in: PS4 microphone open (handle %d, 48 kHz mono S16)", impl_->handle);
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
