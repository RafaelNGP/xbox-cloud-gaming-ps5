// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Input features beyond the common controller state: what each pad can do
// (the UI hides what it can't), the mouse held by the window during a game,
// and the desktop (SDL3) backends' hooks for the shell, which owns the
// window and the event loop. The PS5 and null backends implement the first
// two as well.
#pragma once

union SDL_Event;
struct SDL_Window;

namespace xc::input {

enum class PadKind { None, DualSense, DualShock4, Xbox, Switch, Generic };

struct PadCaps {
    PadKind kind = PadKind::None;   // None: nothing in the slot
    bool rumble = false;
    bool triggerRumble = false;     // Xbox impulse triggers or the DualSense's trigger vibration
    bool adaptiveTriggers = false;  // DualSense resistance / pulses
    bool lightBar = false;
    bool gyro = false;
    bool touchpad = false;
};

// The pad in slot `index` (0..kMaxPads-1).
PadCaps padCaps(int index);
// What the connected pads can do between them, for Settings and the game
// menu. On the PS5 it is always a DualSense's (the console's pads), so the
// PS5 shows every option as before.
PadCaps anyPadCaps();

// The mouse held by the window (relative motion, no cursor) while a game
// has the keyboard and mouse; let go in menus and when the window loses
// focus. A no-op where there is no pointer to hold (PS5, null).
void kbmSetCaptured(bool captured);

// Desktop (SDL3) only: the shell calls these around its event loop.
void sdlInputInit(SDL_Window* window);
void sdlInputEvent(const SDL_Event& event);
void sdlInputShutdown();

}  // namespace xc::input
