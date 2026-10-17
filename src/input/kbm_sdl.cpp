// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The keyboard and mouse on the desktop (kbm.h), from the window's SDL3
// events (sdl_input.cpp). Keys come out as Windows virtual-key codes, as the
// PS5's keyboard does (SDL's scancodes are the same USB HID usages); the
// mouse's movement counts only while the window holds it (kbmSetCaptured:
// in a game), its buttons and wheel while the window has the focus.
//
// A DualSense's (or DS4's) touchpad is not the mouse. Linux's driver gives
// it a pointer device of its own, so the desktop moves the pointer with it
// and SDL hands that on as mouse events, which in a game moved the right
// stick (keyboard as a controller) or the game's cursor. The pad's touchpad
// is read through the pad (SDL_GetGamepadTouchpadFinger), so those events
// are dropped, two ways:
// - by device, where SDL names the mouse a motion came from (X11: each
//   pointer device): isPadPointerName();
// - by time, where it can't: Wayland merges every pointer into the seat's
//   ("Virtual core pointer (seat0)"), so while a finger is on a pad's
//   touchpad, and kTouchGraceNs after (a tap turns into a click a moment
//   after the finger leaves), the mouse is ignored. The pad's touch events
//   come after the pointer's from the same SDL_PumpEvents, so a touch also
//   drops what the mouse gathered since the last poll. This needs the pad
//   opened through HIDAPI (controller_sdl.cpp), which reports its touchpad.
#include "input/input_ext.h"
#include "input/kbm.h"
#include "input/sdl_backend.h"
#include "input/sdl_mapping.h"
#include "util/log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace xc::input {

namespace {

// Events and polls are on the main thread; the lock keeps them whole if a
// shell ever pumps elsewhere. SDL is never called with it held.
std::mutex g_mutex;
SDL_Window* g_window = nullptr;
bool g_captureWanted = false, g_focused = true, g_captured = false;
bool g_down[SDL_SCANCODE_COUNT] = {};  // keys held (scancodes)
std::vector<int> g_pressed;            // scancodes that went down since the last poll
std::vector<uint8_t> g_tapped;         // keys that went down and up since the last poll
float g_dx = 0, g_dy = 0;              // movement while held, what isn't a whole step yet kept
int32_t g_wheel = 0;                   // notches, + away from the user (as Windows counts them)
uint32_t g_sdlButtons = 0;             // SDL's mask, held now
uint32_t g_sdlClicked = 0;             // went down since the last poll (a click between two polls still counts)
// What the last poll reported.
std::vector<uint8_t> g_held;
uint8_t g_buttons = 0;
// The pads' touchpads: fingers on them (pad, touchpad, finger) and when
// the last one left (SDL ns), for kTouchGraceNs.
constexpr uint64_t kTouchGraceNs = 400'000'000;
std::set<uint64_t> g_fingers;
uint64_t g_liftNs = 0;
// Main thread (SDL is called for the names): which mice are a pad's.
std::map<SDL_MouseID, bool> g_padPointer;

// Whether events of mouse `id` are a controller's touchpad (by its name;
// main thread, without g_mutex).
bool padPointer(SDL_MouseID id) {
    if (id == 0 || id == SDL_TOUCH_MOUSEID || id == SDL_PEN_MOUSEID) return false;
    auto it = g_padPointer.find(id);
    if (it != g_padPointer.end()) return it->second;
    const char* name = SDL_GetMouseNameForID(id);
    bool pad = isPadPointerName(name);
    g_padPointer[id] = pad;
    if (pad) XC_LOGI("mouse: '%s' is a controller's touchpad: its events are ignored", name);
    return pad;
}

// Whether the mouse's events now are a pad's touchpad going by time
// (Wayland). Caller holds g_mutex.
bool padTouching(uint64_t nowNs) { return !g_fingers.empty() || (g_liftNs && nowNs - g_liftNs < kTouchGraceNs); }

// Holds or lets go of the mouse: held while a game wants it and the window
// has the focus.
void applyCapture() {
    SDL_Window* window;
    bool hold;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        hold = g_captureWanted && g_focused && g_window;
        if (hold == g_captured) return;
        g_captured = hold;
        if (!hold) g_dx = g_dy = 0;  // what moved before letting go isn't the game's
        window = g_window;
    }
    if (!window) return;
    if (SDL_SetWindowRelativeMouseMode(window, hold))
        XC_LOGI("mouse %s", hold ? "held by the window" : "let go");
    else
        XC_LOGW("SDL_SetWindowRelativeMouseMode(%d): %s", hold, SDL_GetError());
}

bool ourWindow(const SDL_Event& e) {
    SDL_Window* window;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        window = g_window;
    }
    return !window || e.window.windowID == SDL_GetWindowID(window);
}

}  // namespace

void kbmSdlInit(SDL_Window* window) {
    bool focused = !window || (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_window = window;
    g_focused = focused;
}

void kbmSdlEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        if (e.key.repeat) break;
        int sc = e.key.scancode;
        if (sc <= SDL_SCANCODE_UNKNOWN || sc >= SDL_SCANCODE_COUNT) break;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (e.key.down) {
            if (!g_down[sc]) g_pressed.push_back(sc);
            g_down[sc] = true;
        } else {
            bool quick = g_down[sc] && std::find(g_pressed.begin(), g_pressed.end(), sc) != g_pressed.end();
            if (uint8_t vk = scancodeToVk(sc); quick && vk) g_tapped.push_back(vk);
            g_down[sc] = false;
        }
        break;
    }
    case SDL_EVENT_MOUSE_ADDED:
    case SDL_EVENT_MOUSE_REMOVED:
        g_padPointer.erase(e.mdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP: {
        uint64_t key = static_cast<uint64_t>(e.gtouchpad.which) << 16 | static_cast<uint64_t>(e.gtouchpad.touchpad & 0xFF) << 8 |
                       static_cast<uint64_t>(e.gtouchpad.finger & 0xFF);
        std::lock_guard<std::mutex> lock(g_mutex);
        static bool logged = false;
        if (!logged) XC_LOGI("mouse: ignored while a finger is on a pad's touchpad"), logged = true;
        if (e.type == SDL_EVENT_GAMEPAD_TOUCHPAD_UP) {
            g_fingers.erase(key);
            g_liftNs = e.common.timestamp ? e.common.timestamp : SDL_GetTicksNS();
        } else {
            g_fingers.insert(key);
            // The pointer's events from this pump came first: the touchpad's too.
            g_dx = g_dy = 0;
            g_wheel = 0;
            g_sdlClicked = 0;
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_REMOVED: {
        std::lock_guard<std::mutex> lock(g_mutex);
        uint64_t pad = static_cast<uint64_t>(e.gdevice.which);
        for (auto it = g_fingers.begin(); it != g_fingers.end();)
            it = (*it >> 16) == pad ? g_fingers.erase(it) : std::next(it);
        break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        if (padPointer(e.motion.which)) break;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (padTouching(e.common.timestamp)) break;
        if (g_captured) {
            g_dx += e.motion.xrel;
            g_dy += e.motion.yrel;
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (e.button.button < SDL_BUTTON_LEFT || e.button.button > SDL_BUTTON_X2) break;
        if (padPointer(e.button.which)) break;
        uint32_t bit = SDL_BUTTON_MASK(e.button.button);
        std::lock_guard<std::mutex> lock(g_mutex);
        if (e.button.down && padTouching(e.common.timestamp)) break;  // a release always counts
        if (e.button.down) {
            g_sdlButtons |= bit;
            g_sdlClicked |= bit;
        } else {
            g_sdlButtons &= ~bit;
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        if (padPointer(e.wheel.which)) break;
        int32_t notches = e.wheel.integer_y;
        if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) notches = -notches;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (padTouching(e.common.timestamp)) break;
        g_wheel += notches;
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST: {
        if (!ourWindow(e)) break;
        bool focused = e.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_focused = focused;
            if (!focused) {  // nothing stays held for the game behind another window
                std::fill(std::begin(g_down), std::end(g_down), false);
                g_sdlButtons = 0;
            }
        }
        applyCapture();
        break;
    }
    default:
        break;
    }
}

void kbmSdlShutdown() {
    kbmSetCaptured(false);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_window = nullptr;
}

bool kbmInit(int32_t) {
    XC_LOGI("keyboard: %s, mouse: %s (SDL)", SDL_HasKeyboard() ? "found" : "none yet", SDL_HasMouse() ? "found" : "none yet");
    return true;
}

KbmInput kbmPoll() {
    KbmInput out;
    out.keyboard = SDL_HasKeyboard();
    out.mouse = SDL_HasMouse();
    std::lock_guard<std::mutex> lock(g_mutex);

    // The keys held now; what went down or up since the last poll is the
    // difference, as the PS5's keyboard records give it.
    std::vector<uint8_t> held;
    for (int sc = 0; sc < SDL_SCANCODE_COUNT; ++sc)
        if (g_down[sc])
            if (uint8_t vk = scancodeToVk(sc)) held.push_back(vk);
    std::sort(held.begin(), held.end());
    held.erase(std::unique(held.begin(), held.end()), held.end());
    for (uint8_t vk : g_held)
        if (!std::binary_search(held.begin(), held.end(), vk)) out.keys.push_back({vk, false});
    for (uint8_t vk : held)
        if (!std::binary_search(g_held.begin(), g_held.end(), vk)) out.keys.push_back({vk, true});
    // Down and up again between two polls (a quick tap while a frame took
    // long): both, so it isn't lost.
    for (uint8_t vk : g_tapped)
        if (!std::binary_search(held.begin(), held.end(), vk) && !std::binary_search(g_held.begin(), g_held.end(), vk)) {
            out.keys.push_back({vk, true});
            out.keys.push_back({vk, false});
        }
    g_tapped.clear();
    g_pressed.clear();
    g_held.swap(held);
    out.held = g_held;
    static int logged = 0;  // the first few, to check a keyboard works (not what is typed after)
    for (const auto& k : out.keys)
        if (logged < 10) XC_LOGI("keyboard: key 0x%02x %s (%d)", k.vk, k.pressed ? "down" : "up", ++logged);

    // The mouse: whole steps of the movement (the rest kept for the next
    // poll), the wheel, the buttons (a click since the last poll shows once).
    out.dx = static_cast<int32_t>(g_dx);
    out.dy = static_cast<int32_t>(g_dy);
    g_dx -= static_cast<float>(out.dx);
    g_dy -= static_cast<float>(out.dy);
    out.wheel = g_wheel;
    g_wheel = 0;
    uint8_t buttons = sdlMouseButtons(g_sdlButtons | g_sdlClicked);
    g_sdlClicked = 0;
    out.buttons = buttons;
    out.moved = out.dx || out.dy || out.wheel || buttons != g_buttons;
    static int movesLogged = 0;
    if (out.moved && movesLogged < 10)
        XC_LOGI("mouse: %d,%d wheel %d buttons 0x%02x (%d)", out.dx, out.dy, out.wheel, buttons, ++movesLogged);
    g_buttons = buttons;
    return out;
}

void kbmSetCaptured(bool captured) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_captureWanted = captured;
    }
    applyCapture();
}

}  // namespace xc::input
