// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Between sdl_input.cpp (input_ext.h's SDL3 hooks, called by the shell) and
// the SDL backends it hands the window's events to: the gamepads
// (controller_sdl.cpp) and the keyboard and mouse (kbm_sdl.cpp). Main
// thread, as SDL's event loop.
#pragma once

union SDL_Event;
struct SDL_Window;

namespace xc::input {

// Starts SDL's gamepads (unless the shell did) and opens those plugged in.
void padSdlInit();
// SDL_EVENT_GAMEPAD_ADDED / REMOVED.
void padSdlEvent(const SDL_Event& event);
// Closes the pads (their effects stopped).
void padSdlShutdown();

// The window whose events come in and which holds the mouse.
void kbmSdlInit(SDL_Window* window);
// Keys, mouse movement / buttons / wheel, the window's focus.
void kbmSdlEvent(const SDL_Event& event);
// Lets the mouse go.
void kbmSdlShutdown();

}  // namespace xc::input
