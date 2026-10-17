// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// app/shell.h for the desktop app (SDL3): SDL comes up before the display,
// the window (display_sdl.cpp) goes to the input backend, and each pass
// empties SDL's event queue: closing the window (or Ctrl+C, which SDL turns
// into a quit event) ends the app in order, F11 / Alt+Enter switch
// fullscreen (the setting follows), every event goes on to the input backend (pads, keyboard,
// mouse); while a field wants text (setTextInput()) what is typed is kept
// for it. Notices are toasts in the window; links open in the browser; no
// update is installed (Settings opens the release page).
#include "app/shell.h"

#include "display/display_sdl.h"
#include "input/input_ext.h"
#include "platform/toasts.h"
#include "util/log.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace xc::app::shell {

namespace {
bool g_quit = false;              // main thread
int g_switchedTo = -1;            // F11 / Alt+Enter: the fullscreen state asked (main thread), -1 none
std::atomic<bool> g_focus{true};  // the window has the keyboard
bool g_textOn = false;            // SDL text input started (main thread)
std::string g_typed;              // typed since takeTextInput() (main thread)
volatile std::sig_atomic_t g_signalled = 0;
void onSignal(int) { g_signalled = 1; }
}  // namespace

void beforeSettings() {
    platform::enableToasts(true);  // platform::notify() -> a toast in the window
}

void beforeDisplay(const Settings& settings) {
    display::sdlStartFullscreenSetting(settings.desktopFullscreen);  // unless --windowed / --fullscreen
    // The display, the pads and the sound start their own parts; the event
    // loop is ours.
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
        XC_LOGE("SDL: %s", SDL_GetError());
    // Ctrl+C and SIGTERM close the app in order. SDL turns them into a quit
    // event only when they weren't ignored (a shell's background job
    // ignores SIGINT): ours, as psbox-headless does.
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
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
                if (!ev.key.repeat) {
                    bool on = !display::sdlIsFullscreen();
                    display::sdlSetFullscreen(on);
                    g_switchedTo = on;
                }
                continue;
            }
            if (g_textOn && ev.key.key == SDLK_BACKSPACE) g_typed += '\b';  // repeats too
            if (g_textOn && !ev.key.repeat && (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER)) g_typed += '\n';
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (g_textOn && ev.text.text) g_typed += ev.text.text;
            break;
        default: break;
        }
        input::sdlInputEvent(ev);
    }
    if (g_signalled && !g_quit) {
        XC_LOGI("closing on a signal");
        g_quit = true;
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

Caps caps() { return {false, true, false, true}; }

void setFullscreen(bool on) { display::sdlSetFullscreen(on); }

bool fullscreenSwitched(bool& on) {
    if (g_switchedTo < 0) return false;
    on = g_switchedTo == 1;
    g_switchedTo = -1;
    return true;
}

bool openUrl(const std::string& url) {
    if (SDL_OpenURL(url.c_str())) return true;
    XC_LOGW("browser: %s", SDL_GetError());
    return false;
}

bool setTextInput(bool on) {
    if (on == g_textOn) return g_textOn;
    SDL_Window* w = display::sdlWindow();
    if (on) {
        g_textOn = w && SDL_StartTextInput(w);
        static bool told = false;
        if (!g_textOn && !told) told = true, XC_LOGW("text input: %s", SDL_GetError());
    } else {
        if (w) SDL_StopTextInput(w);
        g_textOn = false;
        g_typed.clear();
    }
    return g_textOn;
}

std::string takeTextInput() {
    std::string s;
    s.swap(g_typed);
    return s;
}

void restartOrExit() {
    // Never asked (no update is installed here).
    std::fflush(nullptr);
    std::_Exit(0);
}

}  // namespace xc::app::shell
