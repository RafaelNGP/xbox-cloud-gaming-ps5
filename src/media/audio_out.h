// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Audio playback: 48 kHz interleaved float stereo pushed from any thread,
// played by a dedicated thread in 256-frame grains (silence on underrun).
#pragma once

#include <cstddef>

namespace xc::media {

enum class AudioRoute {
    Main = 0,  // TV / HDMI / Default output
    Pad = 1    // DualSense Controller Speaker / Headset
};

bool audioStart();
void audioStop();
void audioSetRoute(AudioRoute route);
AudioRoute audioRoute();
// The game's volume: 1 as it comes, up to 2 (louder, clipped at full scale).
void audioSetVolume(float gain);
// Past ~60 ms queued (a burst from the network) cuts back to ~40 ms, the
// oldest samples first, to keep the sound close to the picture.
void audioPush(const float* interleaved, size_t frames);
// Plays a short audio sample or chime directly through the DualSense controller speaker,
// falling back to main output if the pad speaker port is not available.
bool playPadSpeakerAudio(const float* interleavedStereo, size_t frames);
// Sets the DualSense pad speaker volume (0 to 100%).
void setPadSpeakerVolume(int percent);
int padSpeakerVolume();
// Returns true if the controller speaker is physically available (DualSense with valid audio port).
bool hasPadSpeaker();
// Voice routing: 0 = Auto (DualSense pad speaker if available, fallback to main), 1 = Always Main (TV/Headset)
void audioSetVoiceRouting(int routing);
int audioVoiceRouting();
// Adaptive voice audio playback: routes to the DualSense controller speaker when
// voice routing is Auto and the controller speaker is physically available,
// or transparently mixes into the main audio channel when voice routing is Always Main
// or when using headsets / third-party controllers without a controller speaker.
bool playVoiceAudio(const float* interleavedStereo, size_t frames);

}  // namespace xc::media
