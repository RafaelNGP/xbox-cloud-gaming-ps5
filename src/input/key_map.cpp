// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/key_map.h"

#include <cstdio>

namespace xc::input {

KeyMap defaultKeyMap() {
    KeyMap m{};
    auto set = [&](KeyAction a, uint8_t k1, uint8_t k2 = 0) { m[static_cast<size_t>(a)] = {k1, k2}; };
    set(KeyAction::LeftUp, 'W');
    set(KeyAction::LeftDown, 'S');
    set(KeyAction::LeftLeft, 'A');
    set(KeyAction::LeftRight, 'D');
    set(KeyAction::DpadUp, 0x26);
    set(KeyAction::DpadDown, 0x28);
    set(KeyAction::DpadLeft, 0x25);
    set(KeyAction::DpadRight, 0x27);
    set(KeyAction::A, 0x20);
    set(KeyAction::B, 'C');
    set(KeyAction::X, 'R');
    set(KeyAction::Y, 'E');
    set(KeyAction::LB, 'Q');
    set(KeyAction::RB, 'F');
    set(KeyAction::LT, kMouseRight);
    set(KeyAction::RT, kMouseLeft);
    set(KeyAction::L3, 0x10);  // shift
    set(KeyAction::R3, 'V', kMouseMiddle);
    set(KeyAction::View, 0x09);  // tab
    set(KeyAction::Menu, 0x1B);  // escape
    return m;
}

std::string keyName(uint8_t k) {
    if ((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9')) return std::string(1, static_cast<char>(k));
    char buf[16];
    if (k >= 0x70 && k <= 0x87) {
        std::snprintf(buf, sizeof buf, "F%d", k - 0x70 + 1);
        return buf;
    }
    if (k >= 0x60 && k <= 0x69) {
        std::snprintf(buf, sizeof buf, "Num %d", k - 0x60);
        return buf;
    }
    switch (k) {
    case 0: return "";
    case kMouseLeft: return "Mouse L";
    case kMouseRight: return "Mouse R";
    case kMouseMiddle: return "Mouse M";
    case kMouseBack: return "Mouse 4";
    case kMouseForward: return "Mouse 5";
    case 0x08: return "Backspace";
    case 0x09: return "Tab";
    case 0x0D: return "Enter";
    case 0x10: return "Shift";
    case 0x11: return "Ctrl";
    case 0x12: return "Alt";
    case 0x13: return "Pause";
    case 0x14: return "Caps Lock";
    case 0x1B: return "Esc";
    case 0x20: return "Space";
    case 0x21: return "Page Up";
    case 0x22: return "Page Down";
    case 0x23: return "End";
    case 0x24: return "Home";
    case 0x25: return "Left";
    case 0x26: return "Up";
    case 0x27: return "Right";
    case 0x28: return "Down";
    case 0x2C: return "Print Screen";
    case 0x2D: return "Insert";
    case 0x2E: return "Delete";
    case 0x5B: return "Win";
    case 0x5C: return "Win R";
    case 0x5D: return "Menu";
    case 0x6A: return "Num *";
    case 0x6B: return "Num +";
    case 0x6D: return "Num -";
    case 0x6E: return "Num .";
    case 0x6F: return "Num /";
    case 0x90: return "Num Lock";
    case 0x91: return "Scroll Lock";
    case 0xBA: return ";";
    case 0xBB: return "=";
    case 0xBC: return ",";
    case 0xBD: return "-";
    case 0xBE: return ".";
    case 0xBF: return "/";
    case 0xC0: return "`";
    case 0xDB: return "[";
    case 0xDC: return "\\";
    case 0xDD: return "]";
    case 0xDE: return "'";
    case 0xE2: return "\\ (ISO)";
    default: std::snprintf(buf, sizeof buf, "0x%02X", k); return buf;
    }
}

bool isReservedKey(uint8_t k) { return k == 0x7B || k == 0x24 || (k >= '1' && k <= '4'); }

void assignKey(KeyMap& map, KeyAction a, int slot, uint8_t code) {
    if (code)
        for (auto& keys : map)
            for (auto& k : keys)
                if (k == code) k = 0;
    map[static_cast<size_t>(a)][static_cast<size_t>(slot & 1)] = code;
}

}  // namespace xc::input
