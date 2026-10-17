// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The desktop's input hooks (input_ext.h): the shell owns the window and
// SDL's event loop and hands each event over; the gamepads go to
// controller_sdl.cpp, keys, the mouse and the window's focus to kbm_sdl.cpp.
#include "input/input_ext.h"
#include "input/sdl_backend.h"

#include <SDL3/SDL.h>

namespace xc::input {

void sdlInputInit(SDL_Window* window) {
    padSdlInit();
    kbmSdlInit(window);
}

void sdlInputEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        padSdlEvent(event);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        padSdlEvent(event);
        kbmSdlEvent(event);  // its fingers off the touchpad
        break;
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
    case SDL_EVENT_MOUSE_ADDED:
    case SDL_EVENT_MOUSE_REMOVED:
        kbmSdlEvent(event);  // a pad's touchpad isn't the mouse
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        kbmSdlEvent(event);
        break;
    default:
        break;
    }
}

void sdlInputShutdown() {
    kbmSdlShutdown();
    padSdlShutdown();
}

}  // namespace xc::input
