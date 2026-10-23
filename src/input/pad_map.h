// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Which of the controller's buttons plays which Xbox button in a game,
// chosen in Settings (and per game) on the "Controller buttons" screen.
//
// The map only changes what the game is sent (applyPadMap(), in the
// stream's input report): the app's menus, its Settings and the in-game
// menu's combo always read the buttons as they are, so a bad map can never
// keep anyone from going back to fix it.
//
// Order with "Circle confirms" (input::setCircleConfirms): that swap of
// Cross and Circle happens first, in input::pollPad(), for the menus and
// the game alike; this map is applied after it, to the game's copy only.
// A map's sources are therefore the ControllerState's slots as the menus
// see them (with the swap on, slot A is the physical Circle);
// physicalSource() turns one back into the button on the pad.
#pragma once

#include "input/controller.h"

#include <array>
#include <cstdint>
#include <string>

namespace xc::json {
class Value;
}

namespace xc::input {

// The Xbox buttons a game is sent, and (the same indices) the pad's
// buttons that can feed them: A B X Y are the south, east, west and north
// face buttons (Cross, Circle, Square, Triangle), LB RB L1 R1, LT RT the
// triggers L2 R2, LS RS the stick clicks, View the touchpad (Create on an
// Xbox-like pad: Back/View), Menu Options. The sticks aren't remapped.
enum class PadFn : uint8_t { A, B, X, Y, LB, RB, LT, RT, LS, RS, View, Menu, Up, Down, Left, Right, Count };
constexpr int kPadFns = static_cast<int>(PadFn::Count);
// No button (a row cleared on the screen; never valid to keep).
constexpr uint8_t kPadNone = 0xFF;

// map[fn]: the source (a PadFn index) that feeds fn, or kPadNone.
using PadMap = std::array<uint8_t, kPadFns>;

// Every button plays itself.
PadMap defaultPadMap();
// The game's copy of `in`: each Xbox button pressed while its source is.
// A trigger fed by a trigger keeps how far it is pressed; fed by a button,
// it is fully pressed with it. A button fed by a trigger is pressed past
// halfway. Sticks, touch and the Xbox button pass as they are.
ControllerState applyPadMap(const ControllerState& in, const PadMap& map);
// The sources pressed now, a bit per PadFn index (triggers past halfway).
uint32_t padButtons(const ControllerState& s);
// Puts `source` on `fn`. A source already feeding another button swaps
// with it (that one gets fn's old source), so a complete map stays complete.
void assignPadSource(PadMap& map, PadFn fn, uint8_t source);
// The Xbox buttons with no source, a bit per PadFn (0: none missing).
uint32_t padMapMissing(const PadMap& map);
// Every Xbox button has exactly one source and no source feeds two.
bool padMapValid(const PadMap& map);
// The button on the pad behind a source, with "Circle confirms" on or off
// (A and B swapped back).
uint8_t physicalSource(uint8_t source, bool circleConfirms);
// "A", "LB", "View", "D-pad up"... (log and test names).
const char* padFnName(PadFn fn);

// {"version":1,"map":[16 sources]} in PadFn order.
json::Value padMapJson(const PadMap& map);
// Reads it back into `out`. Anything else (another version, size, a
// source out of range, an incomplete or duplicated map) gives false and
// `out` the default map.
bool readPadMap(const json::Value& v, PadMap& out);

enum class TouchpadAction : uint8_t {
    None = 0,
    GameMenu = 1,
    XboxButton = 2,
    ToggleStats = 3,
    ToggleMicMute = 4,
    CycleGyro = 5,
    CycleTriggerProfile = 6,
    View = 7,
    Menu = 8,
    L3 = 9,
    R3 = 10,
    A = 11,
    B = 12,
    X = 13,
    Y = 14,
    LB = 15,
    RB = 16,
    DpadUp = 17,
    DpadDown = 18,
    DpadLeft = 19,
    DpadRight = 20,
    Count = 21
};
constexpr int kTouchpadActions = static_cast<int>(TouchpadAction::Count);
constexpr int kTouchpadTriggers = 6;

struct TouchpadShortcuts {
    TouchpadAction swipeUp = TouchpadAction::XboxButton;
    TouchpadAction swipeDown = TouchpadAction::GameMenu;
    TouchpadAction swipeLeft = TouchpadAction::GameMenu;
    TouchpadAction swipeRight = TouchpadAction::XboxButton;
    TouchpadAction clickLeft = TouchpadAction::View;
    TouchpadAction clickRight = TouchpadAction::View;

    bool operator==(const TouchpadShortcuts& o) const {
        return swipeUp == o.swipeUp && swipeDown == o.swipeDown &&
               swipeLeft == o.swipeLeft && swipeRight == o.swipeRight &&
               clickLeft == o.clickLeft && clickRight == o.clickRight;
    }
    bool operator!=(const TouchpadShortcuts& o) const { return !(*this == o); }

    TouchpadAction get(int triggerIndex) const {
        switch (triggerIndex) {
        case 0: return swipeUp;
        case 1: return swipeDown;
        case 2: return swipeLeft;
        case 3: return swipeRight;
        case 4: return clickLeft;
        case 5: return clickRight;
        default: return TouchpadAction::None;
        }
    }

    void set(int triggerIndex, TouchpadAction act) {
        if (static_cast<uint8_t>(act) >= static_cast<uint8_t>(TouchpadAction::Count))
            act = TouchpadAction::None;
        switch (triggerIndex) {
        case 0: swipeUp = act; break;
        case 1: swipeDown = act; break;
        case 2: swipeLeft = act; break;
        case 3: swipeRight = act; break;
        case 4: clickLeft = act; break;
        case 5: clickRight = act; break;
        default: break;
        }
    }
};

TouchpadShortcuts defaultTouchpadShortcuts();
json::Value touchpadShortcutsJson(const TouchpadShortcuts& ts);
bool readTouchpadShortcuts(const json::Value& v, TouchpadShortcuts& out);
bool isTouchpadButtonAction(TouchpadAction act);
bool applyTouchpadButtonAction(ControllerState& s, TouchpadAction act);

}  // namespace xc::input
