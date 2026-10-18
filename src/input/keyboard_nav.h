// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The app's screens with a keyboard: the keys held (Windows virtual-key
// codes, whatever reads them: the PS5's library, SDL on a desktop) become
// the controller buttons the screens already answer to.
//
//   arrows / W A S D    D-pad              Enter / Space   Cross (select)
//   Esc / Backspace     Circle (back)      Tab             OPTIONS (settings)
//   F / "/"             Triangle (search)  H / X           Square (hide)
//   Q / E               L1 / R1 (tabs)     Z / C           L2 / R2
//   Page Up / Down      right stick (scroll by pages)      R  R3
//   Delete              touchpad (held: sign out)
//
// In a game the keys go to the game; F12 opens and closes the app's menu.
#pragma once

#include "input/controller.h"
#include "input/kbm.h"
#include "input/key_map.h"

#include <cstdint>
#include <vector>

namespace xc::input {

// The key that opens the app's menu during a game (F12).
constexpr uint8_t kVkAppMenu = 0x7B;

// The keyboard and mouse as a controller of their own (another player, so
// a second controller isn't needed): the keys as `map` says (defaultKeyMap()
// unless changed in Settings), the mouse the right stick, a wheel notch a
// press of Y.
//
// Timed by the clock (`nowMs`, monotonic), not by the passes: the mouse's
// speed (counts per ms), the stick's easing and the wheel's presses come
// out the same however often it runs (the stream's input loop, 4 or 8 ms;
// the desktop's, woken by events).
struct MouseStick {
    float x = 0, y = 0;  // the right stick, eased towards the mouse's speed
    uint64_t lastMs = 0;  // the previous pass (0: none yet)
    // The wheel: each notch is a short press of Y (switch weapon in most
    // shooters): presses still to give, and when the current one began.
    int wheelPresses = 0;
    bool wheelPressing = false;
    uint64_t wheelSinceMs = 0;
};
void keyboardAsGamepad(const KbmInput& in, MouseStick& stick, ControllerState& out, uint64_t nowMs,
                       const KeyMap& map = defaultKeyMap());

// Adds `extra` (the keyboard as a controller) to `pad`: buttons pressed on
// either, the stick or trigger pushed further.
void mergePads(ControllerState& pad, const ControllerState& extra);

// Adds what the keys held press to `pad` (buttons on, sticks pushed); the
// controller's own input stays.
void keyboardToPad(const std::vector<uint8_t>& held, ControllerState& pad);

// The DualSense's touchpad as a mouse in games that take native mouse and
// keyboard: one finger moves the cursor and a light tap clicks left; two
// fingers scroll vertically and a light tap clicks right; clicking the pad
// after moving the cursor holds left (or right on the right third / with two
// fingers) for dragging while keeping a plain click as the controller's View.
struct TouchpadMouse {
    bool touching = false, second = false, hadSecond = false, moved = false, dragging = false;
    float x = 0, y = 0, startX = 0, startY = 0;
    float remX = 0, remY = 0, remWheel = 0;
    uint64_t downMs = 0, tapUntilMs = 0;
    uint8_t tapButton = 0, dragButton = 0, prevButtons = 0;
};
// Adds what the touchpad does to `io` (dx, dy, wheel, buttons, moved); returns
// true while a physical touchpad click is held as a mouse button (so the
// controller doesn't also press View).
bool touchpadAsMouse(const ControllerState& pad, uint64_t nowMs, TouchpadMouse& state, KbmInput& io);

}  // namespace xc::input
