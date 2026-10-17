// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The microphone on the desktop (audio_in.h): SDL3's default recording
// device, 48 kHz mono S16, handed to audio_in_common.h's gain, mute, meter
// and 20 ms frames in the PS5's 256-sample grains, on SDL's device thread.
// No system mute is read here (isHardwareMuted() stays false).
#include "media/audio_in.h"
#include "media/audio_in_common.h"
#include "util/log.h"

#include <SDL3/SDL.h>

#include <vector>

namespace xc::media {

struct AudioIn::Impl {
    SDL_AudioStream* stream = nullptr;
    bool initedAudio = false;  // SDL's audio subsystem started here (not by the shell)
    MicPipeline mic;
    std::vector<int16_t> grain = std::vector<int16_t>(MicPipeline::kGrain);
    uint64_t grains = 0;
};

AudioIn::AudioIn() : impl_(std::make_unique<Impl>()) {}

AudioIn::~AudioIn() {
    close();
}

bool AudioIn::open(AudioCallback cb) {
    if (running_) return true;
    callback_ = std::move(cb);

    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            XC_LOGE("audio in: SDL audio: %s", SDL_GetError());
            return false;
        }
        impl_->initedAudio = true;
    }
    impl_->mic.reset();
    impl_->grains = 0;
    // SDL's device thread, as samples come in: whole grains through the
    // pipeline, the rest left in the stream for next time.
    auto put = [](void* self, SDL_AudioStream* stream, int, int) {
        AudioIn& in = *static_cast<AudioIn*>(self);
        Impl& impl = *in.impl_;
        const int bytes = static_cast<int>(MicPipeline::kGrain * sizeof(int16_t));
        while (in.running_ && SDL_GetAudioStreamAvailable(stream) >= bytes) {
            if (SDL_GetAudioStreamData(stream, impl.grain.data(), bytes) != bytes) break;
            if (++impl.grains == 1) XC_LOGI("audio in: first input buffer received (%zu samples)", MicPipeline::kGrain);
            impl.mic.process(impl.grain.data(), impl.grain.size(), in.muted_.load(std::memory_order_relaxed),
                             in.gain_.load(std::memory_order_relaxed), in.level_, in.callback_);
        }
    };
    const SDL_AudioSpec spec{SDL_AUDIO_S16, kChannels, kSampleRate};
    running_ = true;
    impl_->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, put, this);
    if (!impl_->stream) {
        running_ = false;
        XC_LOGE("audio in: SDL_OpenAudioDeviceStream (recording): %s", SDL_GetError());
        if (impl_->initedAudio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
        impl_->initedAudio = false;
        return false;
    }
    SDL_AudioSpec device{};
    int frames = 0;
    SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(impl_->stream), &device, &frames);
    SDL_ResumeAudioStreamDevice(impl_->stream);
    XC_LOGI("audio in: SDL microphone opened (48 kHz mono S16; device %d Hz, %d channels, %d-frame buffer)", device.freq,
            device.channels, frames);
    return true;
}

void AudioIn::close() {
    if (!running_) return;
    running_ = false;
    level_.store(0.0f, std::memory_order_relaxed);
    hardwareMuted_.store(false, std::memory_order_relaxed);

    if (impl_->stream) {
        SDL_DestroyAudioStream(impl_->stream);  // no more callbacks once it returns
        impl_->stream = nullptr;
    }
    if (impl_->initedAudio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    impl_->initedAudio = false;
    callback_ = nullptr;
    XC_LOGI("audio in: closed");
}

}  // namespace xc::media
