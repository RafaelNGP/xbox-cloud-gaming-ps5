// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/kbm.h"

namespace xc::input {

uint8_t hidToVk(uint16_t u) {
    if (u >= 0x04 && u <= 0x1D) return static_cast<uint8_t>('A' + (u - 0x04));  // a..z
    if (u >= 0x1E && u <= 0x26) return static_cast<uint8_t>('1' + (u - 0x1E));  // 1..9
    if (u >= 0x3A && u <= 0x45) return static_cast<uint8_t>(0x70 + (u - 0x3A));  // F1..F12
    if (u >= 0x59 && u <= 0x61) return static_cast<uint8_t>(0x61 + (u - 0x59));  // keypad 1..9
    switch (u) {
    case 0x27: return '0';
    case 0x28: return 0x0D;  // enter
    case 0x29: return 0x1B;  // escape
    case 0x2A: return 0x08;  // backspace
    case 0x2B: return 0x09;  // tab
    case 0x2C: return 0x20;  // space
    case 0x2D: return 0xBD;  // - _
    case 0x2E: return 0xBB;  // = +
    case 0x2F: return 0xDB;  // [ {
    case 0x30: return 0xDD;  // ] }
    case 0x31: return 0xDC;  // \ |
    case 0x32: return 0xDC;  // non-US # ~
    case 0x33: return 0xBA;  // ; :
    case 0x34: return 0xDE;  // ' "
    case 0x35: return 0xC0;  // ` ~
    case 0x36: return 0xBC;  // , <
    case 0x37: return 0xBE;  // . >
    case 0x38: return 0xBF;  // / ?
    case 0x39: return 0x14;  // caps lock
    case 0x46: return 0x2C;  // print screen
    case 0x47: return 0x91;  // scroll lock
    case 0x48: return 0x13;  // pause
    case 0x49: return 0x2D;  // insert
    case 0x4A: return 0x24;  // home
    case 0x4B: return 0x21;  // page up
    case 0x4C: return 0x2E;  // delete
    case 0x4D: return 0x23;  // end
    case 0x4E: return 0x22;  // page down
    case 0x4F: return 0x27;  // right
    case 0x50: return 0x25;  // left
    case 0x51: return 0x28;  // down
    case 0x52: return 0x26;  // up
    case 0x53: return 0x90;  // num lock
    case 0x54: return 0x6F;  // keypad /
    case 0x55: return 0x6A;  // keypad *
    case 0x56: return 0x6D;  // keypad -
    case 0x57: return 0x6B;  // keypad +
    case 0x58: return 0x0D;  // keypad enter
    case 0x62: return 0x60;  // keypad 0
    case 0x63: return 0x6E;  // keypad .
    case 0x64: return 0xE2;  // ISO \ |
    case 0x65: return 0x5D;  // menu
    default: return 0;
    }
}

uint8_t modifierVk(int bit) {
    static constexpr uint8_t kVk[8] = {0x11, 0x10, 0x12, 0x5B, 0x11, 0x10, 0x12, 0x5C};
    return bit >= 0 && bit < 8 ? kVk[bit] : 0;
}

}  // namespace xc::input
