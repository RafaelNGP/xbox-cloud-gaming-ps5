// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Which keys (and mouse buttons) play which controller input when the
// keyboard and mouse are a player's controller: two per input, chosen in
// Settings (and per game). The mouse itself stays the right stick and a
// wheel notch a press of Y.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace xc::input {

enum class KeyAction : uint8_t {
    LeftUp, LeftDown, LeftLeft, LeftRight,  // the left stick
    DpadUp, DpadDown, DpadLeft, DpadRight,
    A, B, X, Y, LB, RB, LT, RT, L3, R3, View, Menu,
    Count
};
constexpr int kKeyActions = static_cast<int>(KeyAction::Count);

// Mouse buttons in a key's place: codes no keyboard sends (Windows keeps
// 0xF1..0xF5 for OEM keys). KbmInput::buttons bit b is kMouseLeft + b.
constexpr uint8_t kMouseLeft = 0xF1, kMouseRight = 0xF2, kMouseMiddle = 0xF3, kMouseBack = 0xF4, kMouseForward = 0xF5;

// Each input's two keys (0: none).
using KeyMap = std::array<std::array<uint8_t, 2>, kKeyActions>;

// W A S D the left stick, the arrows the D-pad; Space A, C B, R X, E Y, Q LB,
// F RB, the mouse's right / left buttons LT / RT, Shift and V or the middle
// button the sticks' clicks, Tab View, Esc Menu.
KeyMap defaultKeyMap();
// A key or mouse button as it is written on it ("W", "Space", "Mouse L").
std::string keyName(uint8_t code);
// Keys the app keeps for itself: F12 (its menu), Home (the Xbox button),
// 1-4 (which player the keyboard is).
bool isReservedKey(uint8_t code);
// Puts `code` in action `a`'s slot, taking it off any other input first.
void assignKey(KeyMap& map, KeyAction a, int slot, uint8_t code);

}  // namespace xc::input
