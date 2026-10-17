// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// 'psbox --selftest <dir>': the desktop app's parts checked without an
// account or the network (SDL_VIDEO_DRIVER=offscreen and
// SDL_AUDIO_DRIVER=dummy work): the window and renderer, screenshots of the
// sign-in and Settings screens, a colour-bar video frame alone and under
// the game menu, the input hooks, the sound out and in.
#pragma once

#include <string>

namespace xc::app {

// Screenshots into `dir` (PNG); 0 when every check passed.
int desktopSelftest(const std::string& dir);

}  // namespace xc::app
