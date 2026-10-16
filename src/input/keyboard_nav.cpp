// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/keyboard_nav.h"

#include <algorithm>

namespace xc::input {

void keyboardToPad(const std::vector<uint8_t>& held, ControllerState& pad) {
    auto down = [&](std::initializer_list<uint8_t> vks) {
        for (uint8_t vk : vks)
            if (std::find(held.begin(), held.end(), vk) != held.end()) return true;
        return false;
    };
    pad.dpadUp |= down({0x26, 'W'});
    pad.dpadDown |= down({0x28, 'S'});
    pad.dpadLeft |= down({0x25, 'A'});
    pad.dpadRight |= down({0x27, 'D'});
    pad.btnA |= down({0x0D, 0x20});    // enter, space
    pad.btnB |= down({0x1B, 0x08});    // escape, backspace
    pad.btnOptions |= down({0x09});    // tab
    pad.btnY |= down({'F', 0xBF});     // f, slash
    pad.btnX |= down({'H', 'X'});
    pad.btnL1 |= down({'Q'});
    pad.btnR1 |= down({'E'});
    pad.btnR3 |= down({'R'});
    pad.btnTouchpad |= down({0x2E});   // delete
    if (down({'Z'})) pad.triggerL2 = 1.0f;
    if (down({'C'})) pad.triggerR2 = 1.0f;
    if (down({0x21})) pad.rightStickY = -1.0f;  // page up
    if (down({0x22})) pad.rightStickY = 1.0f;   // page down
}

}  // namespace xc::input
