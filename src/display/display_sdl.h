// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Between display_sdl.cpp (display.h on SDL3's renderer) and the desktop
// shell and its self-test: the window they share. Main thread only.
#pragma once

struct SDL_Window;

namespace xc::display {

// Before init(): the window opens fullscreen (the desktop's own mode, no
// mode change) or not. The command line's choice (--fullscreen /
// --windowed) wins over the setting's for that run.
void sdlStartFullscreen(bool on);           // the command line
void sdlStartFullscreenSetting(bool on);    // settings.json (desktopFullscreen)
// The app's window once init() made it; null before (or without one).
SDL_Window* sdlWindow();
// The renderer's name ("vulkan", "opengl", "software"...), for the log.
const char* sdlRendererName();
// The window was resized, shown again or uncovered: the last picture is
// presented again at the next pump().
void sdlRepaint();
// Fullscreen on or off (Settings > Display mode); F11 / Alt+Enter toggle.
void sdlSetFullscreen(bool on);
void sdlToggleFullscreen();
bool sdlIsFullscreen();

}  // namespace xc::display
