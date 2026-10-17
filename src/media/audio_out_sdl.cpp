// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Sound out on the desktop (audio_out.h): SDL3's default playback device,
// 48 kHz float stereo, pulling from the shared queue (audio_queue.h) as the
// device asks for more (silence on underrun). It asks for 256-frame device
// buffers, as the PS5's grain; the system may give others (logged).
// Both routes play on the default device (the system picks the speakers or
// a headset).
#include "media/audio_out.h"

#include "media/audio_queue.h"
#include "util/log.h"

#include <SDL3/SDL.h>

#include <mutex>
#include <vector>

namespace xc::media {

namespace {

std::mutex g_mutex;  // start / stop
SDL_AudioStream* g_stream = nullptr;
bool g_initedAudio = false;  // SDL's audio subsystem started here (not by the shell)
std::vector<float> g_buffer;  // the device thread's

// SDL's device thread: `needed` more bytes wanted.
void SDLCALL feed(void*, SDL_AudioStream* stream, int needed, int) {
    if (needed <= 0) return;
    size_t frames = (static_cast<size_t>(needed) + 2 * sizeof(float) - 1) / (2 * sizeof(float));
    if (g_buffer.size() < frames * 2) g_buffer.resize(frames * 2);
    audioQueue().pull(g_buffer.data(), frames);
    SDL_PutAudioStreamData(stream, g_buffer.data(), static_cast<int>(frames * 2 * sizeof(float)));
}

}  // namespace

bool audioStart() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_stream) return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            XC_LOGE("SDL audio: %s", SDL_GetError());
            return false;
        }
        g_initedAudio = true;
    }
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, AudioQueue::kRate};
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, nullptr);
    if (!g_stream) {
        XC_LOGE("SDL_OpenAudioDeviceStream (playback): %s", SDL_GetError());
        if (g_initedAudio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
        g_initedAudio = false;
        return false;
    }
    SDL_AudioSpec device{};
    int frames = 0;
    SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(g_stream), &device, &frames);
    audioQueue().open();
    SDL_ResumeAudioStreamDevice(g_stream);
    const char* driver = SDL_GetCurrentAudioDriver();
    XC_LOGI("audio output open (48 kHz float stereo; SDL %s device: %d Hz, %d channels, %d-frame buffer, %.1f ms)",
            driver ? driver : "?", device.freq, device.channels, frames,
            device.freq > 0 ? frames * 1000.0 / device.freq : 0.0);
    return true;
}

void audioStop() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_stream) return;
    audioQueue().close();
    SDL_DestroyAudioStream(g_stream);  // the device with it; no more feeding once it returns
    g_stream = nullptr;
    audioQueue().clear();
    if (g_initedAudio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    g_initedAudio = false;
    XC_LOGI("audio output closed");
}

}  // namespace xc::media
