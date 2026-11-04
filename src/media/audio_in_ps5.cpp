// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The microphone on the PS5 (audio_in.h): libSceAudioIn read by a thread of
// its own in 256-sample grains; gain, mute, the meter and the 20 ms frames
// are audio_in_common.h's. audio_in_sdl.cpp and audio_in_null.cpp stand in
// elsewhere.
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
            XC_LOGW("sceUserServiceGetInitialUser: 0x%08x", static_cast<unsigned>(rc));
        }
    }
    XC_LOGI("audio in: opening port for user 0x%08x (%d)...", static_cast<unsigned>(userId), userId);

    constexpr unsigned kGrain = MicPipeline::kGrain;
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
    impl_->mic.setFrameSize(frameSize_);

    impl_->thread = std::thread([this]() {
        platform::setThreadAffinity(platform::ThreadRole::Audio, "ps5_audio_in");
        std::vector<int16_t> grain(kGrain);
        unsigned lastStatus = 0xFFFFFFFF;
        uint64_t loopCount = 0;
        // The capture's pace, every 10 s (issue #187, a mic heard late): the
        // rate the port really delivers, its longest wait and how many
        // grains came at once (a backlog being read).
        uint64_t windowStartUs = 0, lastReturnUs = 0, maxGapUs = 0, windowGrains = 0, bunched = 0;

        while (running_) {
            int rc = sceAudioInInput(impl_->handle, grain.data());
            if (rc < 0) {
                XC_LOGE("sceAudioInInput error: 0x%08x", static_cast<unsigned>(rc));
                break;
            }
            if (++loopCount == 1) {
                XC_LOGI("audio in: first input buffer received (%u samples)", kGrain);
            }
            const uint64_t nowUs = platform::nowUs();
            if (!windowStartUs) windowStartUs = nowUs;
            if (lastReturnUs) {
                const uint64_t gap = nowUs - lastReturnUs;
                if (gap > maxGapUs) maxGapUs = gap;
                if (gap < 1000) ++bunched;
            }
            lastReturnUs = nowUs;
            ++windowGrains;
            if (nowUs - windowStartUs >= 10000000) {
                const double secs = static_cast<double>(nowUs - windowStartUs) / 1e6;
                XC_LOGI("audio in: %.0f samples/s over %.1f s, longest wait %.1f ms, %llu of %llu grains at once",
                        static_cast<double>(windowGrains * kGrain) / secs, secs, static_cast<double>(maxGapUs) / 1000.0,
                        static_cast<unsigned long long>(bunched), static_cast<unsigned long long>(windowGrains));
                windowStartUs = nowUs, maxGapUs = 0, windowGrains = 0, bunched = 0;
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

    XC_LOGI("audio in: PS5 audio input opened (handle %d, 48 kHz mono S16)", impl_->handle);
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

}  // namespace xc::media
