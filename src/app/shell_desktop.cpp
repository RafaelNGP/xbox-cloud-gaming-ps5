// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// app/shell.h for the desktop app (SDL3): SDL comes up before the display,
// the window (display_sdl.cpp) goes to the input backend, and each pass
// empties SDL's event queue: closing the window (or Ctrl+C, which SDL turns
// into a quit event) ends the app in order, F11 / Alt+Enter switch
// fullscreen, every event goes on to the input backend (pads, keyboard,
// mouse). Notices are toasts in the window; links open in the browser; no
// update is installed (Settings opens the release page).
#include "app/shell.h"

#include "display/display_sdl.h"
#include "input/input_ext.h"
#include "platform/toasts.h"
#include "util/log.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace xc::app::shell {

namespace {
bool g_quit = false;              // main thread
std::atomic<bool> g_focus{true};  // the window has the keyboard
}  // namespace

void beforeSettings() {
    platform::enableToasts(true);  // platform::notify() -> a toast in the window
}

void beforeDisplay(const Settings&) {
    // The display, the pads and the sound start their own parts; the event
    // loop is ours.
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
        XC_LOGE("SDL: %s", SDL_GetError());
}

bool afterDisplay(bool haveDisplay) {
    if (!haveDisplay) {
        XC_LOGE("no window could be opened: closing");
        return false;
    }
    input::sdlInputInit(display::sdlWindow());
    return true;
}

std::vector<std::string> iconFontPaths() { return {}; }  // the UI draws its own icons

bool pumpEvents() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (!g_quit) XC_LOGI("the window was closed");
            g_quit = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            g_focus = false;
            input::kbmSetCaptured(false);  // the mouse is the system's again at once
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: g_focus = true; break;
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            display::sdlRepaint();
            break;
        case SDL_EVENT_KEY_DOWN:
            // F11 / Alt+Enter: fullscreen. Not passed on (Enter would accept).
            if (ev.key.key == SDLK_F11 || (ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT))) {
                if (!ev.key.repeat) display::sdlToggleFullscreen();
                continue;
            }
            break;
        default: break;
        }
        input::sdlInputEvent(ev);
    }
    return !g_quit;
}

bool hasFocus() { return g_focus; }
bool canSelfUpdate() { return false; }
bool isInstalledCopy(const std::string&) { return false; }

void beforeShutdown() {
    input::kbmSetCaptured(false);
    input::sdlInputShutdown();
}

Caps caps() { return {false, true, false}; }

bool openUrl(const std::string& url) {
    if (SDL_OpenURL(url.c_str())) return true;
    XC_LOGW("browser: %s", SDL_GetError());
    return false;
}

void restartOrExit() {
    // Never asked (no update is installed here).
    std::fflush(nullptr);
    std::_Exit(0);
}

}  // namespace xc::app::shell
