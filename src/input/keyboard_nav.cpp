// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/keyboard_nav.h"

#include <algorithm>
#include <cmath>

namespace xc::input {

void keyboardAsGamepad(const KbmInput& in, MouseStick& stick, ControllerState& out, const KeyMap& map) {
    out = ControllerState{};
    out.connected = in.keyboard || in.mouse;
    auto down = [&](KeyAction a) {
        for (uint8_t k : map[static_cast<size_t>(a)]) {
            if (k >= kMouseLeft && k <= kMouseForward) {
                if (in.buttons & (1u << (k - kMouseLeft))) return true;
            } else if (k && std::binary_search(in.held.begin(), in.held.end(), k)) {
                return true;
            }
        }
        return false;
    };
    out.leftStickX = (down(KeyAction::LeftRight) ? 1.0f : 0.0f) - (down(KeyAction::LeftLeft) ? 1.0f : 0.0f);
    out.leftStickY = (down(KeyAction::LeftDown) ? 1.0f : 0.0f) - (down(KeyAction::LeftUp) ? 1.0f : 0.0f);  // pad convention: +down
    out.dpadUp = down(KeyAction::DpadUp);
    out.dpadDown = down(KeyAction::DpadDown);
    out.dpadLeft = down(KeyAction::DpadLeft);
    out.dpadRight = down(KeyAction::DpadRight);
    out.btnA = down(KeyAction::A);
    out.btnB = down(KeyAction::B);
    out.btnX = down(KeyAction::X);
    out.btnY = down(KeyAction::Y);
    out.btnL1 = down(KeyAction::LB);
    out.btnR1 = down(KeyAction::RB);
    out.btnL3 = down(KeyAction::L3);
    out.btnR3 = down(KeyAction::R3);
    out.btnTouchpad = down(KeyAction::View);
    out.btnOptions = down(KeyAction::Menu);
    out.triggerL2 = down(KeyAction::LT) ? 1.0f : 0.0f;
    out.triggerR2 = down(KeyAction::RT) ? 1.0f : 0.0f;
    // The mouse: about 12 counts a pass is the stick all the way; eased so
    // a still mouse lets the stick come back to the centre.
    constexpr float kFull = 12.0f, kEase = 0.45f;
    float tx = std::clamp(in.dx / kFull, -1.0f, 1.0f), ty = std::clamp(in.dy / kFull, -1.0f, 1.0f);
    stick.x += (tx - stick.x) * kEase;
    stick.y += (ty - stick.y) * kEase;
    if (std::abs(stick.x) < 0.02f) stick.x = 0;
    if (std::abs(stick.y) < 0.02f) stick.y = 0;
    out.rightStickX = stick.x;
    out.rightStickY = stick.y;
    // The wheel: a press of Y per notch, ~50 ms down and ~30 ms up so the
    // game sees each one (a pass is ~8 ms).
    constexpr int kDown = 6, kCycle = 10;
    stick.wheelPresses = std::min(stick.wheelPresses + std::abs(in.wheel), 4);
    if (stick.wheelPresses > 0) {
        if (stick.wheelPhase < kDown) out.btnY = true;
        if (++stick.wheelPhase >= kCycle) {
            stick.wheelPhase = 0;
            --stick.wheelPresses;
        }
    }
}

void mergePads(ControllerState& pad, const ControllerState& e) {
    pad.dpadUp |= e.dpadUp;
    pad.dpadDown |= e.dpadDown;
    pad.dpadLeft |= e.dpadLeft;
    pad.dpadRight |= e.dpadRight;
    pad.btnA |= e.btnA;
    pad.btnB |= e.btnB;
    pad.btnX |= e.btnX;
    pad.btnY |= e.btnY;
    pad.btnL1 |= e.btnL1;
    pad.btnR1 |= e.btnR1;
    pad.btnL3 |= e.btnL3;
    pad.btnR3 |= e.btnR3;
    pad.btnOptions |= e.btnOptions;
    pad.btnTouchpad |= e.btnTouchpad;
    auto further = [](float& a, float b) {
        if (std::abs(b) > std::abs(a)) a = b;
    };
    further(pad.leftStickX, e.leftStickX);
    further(pad.leftStickY, e.leftStickY);
    further(pad.rightStickX, e.rightStickX);
    further(pad.rightStickY, e.rightStickY);
    pad.triggerL2 = std::max(pad.triggerL2, e.triggerL2);
    pad.triggerR2 = std::max(pad.triggerR2, e.triggerR2);
}

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
