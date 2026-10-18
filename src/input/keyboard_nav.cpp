// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/keyboard_nav.h"

#include <algorithm>
#include <cmath>

namespace xc::input {

void keyboardAsGamepad(const KbmInput& in, MouseStick& stick, ControllerState& out, uint64_t nowMs,
                       const KeyMap& map) {
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
    // The time this pass's mouse counts cover: since the previous pass, 8 ms
    // for the first one; at most 50 ms (after the app's menu, say), so a
    // long pause doesn't read as a slow mouse.
    float dtMs = stick.lastMs ? std::clamp(static_cast<float>(nowMs - stick.lastMs), 1.0f, 50.0f) : 8.0f;
    stick.lastMs = nowMs;
    // The mouse: 1.5 counts a millisecond is the stick all the way (the 12
    // counts per 8 ms pass it was tuned with); eased so a still mouse lets
    // the stick come back to the centre: 45 % of the way every 8 ms, the
    // same curve at any pass length.
    constexpr float kFullPerMs = 12.0f / 8.0f, kEase8Ms = 0.45f;
    float full = kFullPerMs * dtMs;
    float tx = std::clamp(in.dx / full, -1.0f, 1.0f), ty = std::clamp(in.dy / full, -1.0f, 1.0f);
    float ease = 1.0f - std::pow(1.0f - kEase8Ms, dtMs / 8.0f);
    stick.x += (tx - stick.x) * ease;
    stick.y += (ty - stick.y) * ease;
    if (std::abs(stick.x) < 0.02f) stick.x = 0;
    if (std::abs(stick.y) < 0.02f) stick.y = 0;
    out.rightStickX = stick.x;
    out.rightStickY = stick.y;
    // The wheel: a press of Y per notch, ~50 ms down and ~30 ms up so the
    // game sees each one, whatever the pass length.
    constexpr uint64_t kDownMs = 48, kCycleMs = 80;
    stick.wheelPresses = std::min(stick.wheelPresses + std::abs(in.wheel), 4);
    if (stick.wheelPresses > 0) {
        if (!stick.wheelPressing) {
            stick.wheelPressing = true;
            stick.wheelSinceMs = nowMs;
        }
        uint64_t t = nowMs - stick.wheelSinceMs;
        if (t >= kCycleMs) {  // this press is over: the next one, if any, starts now
            --stick.wheelPresses;
            stick.wheelPressing = stick.wheelPresses > 0;
            stick.wheelSinceMs = nowMs;
            t = 0;
        }
        if (stick.wheelPressing && t < kDownMs) out.btnY = true;
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

bool touchpadAsMouse(const ControllerState& pad, uint64_t nowMs, TouchpadMouse& s, KbmInput& io) {
    // Full sweep across the touchpad = 960 x 540 counts (1920 x 1080 in the stream).
    constexpr float kScaleX = 960.0f, kScaleY = 540.0f, kWheelScale = 8.0f, kMoveThresh = 0.025f;
    constexpr uint64_t kTapMaxMs = 200, kTapHoldMs = 50;

    if (pad.touching) {
        if (!s.touching) {
            s.touching = true;
            s.second = s.hadSecond = pad.touchSecond;
            s.moved = false;
            s.dragging = false;
            s.dragButton = 0;
            s.x = s.startX = pad.touchX;
            s.y = s.startY = pad.touchY;
            s.remX = s.remY = s.remWheel = 0;
            s.downMs = pad.btnTouchpad ? 0 : nowMs;
        } else {
            if (pad.touchSecond != s.second) {
                s.second = pad.touchSecond;
                if (pad.touchSecond) s.hadSecond = true;
                // Re-anchor so a second finger coming down or lifting doesn't jump the cursor.
                s.x = pad.touchX;
                s.y = pad.touchY;
                s.remX = s.remY = 0;
            }
            if (std::hypot(pad.touchX - s.startX, pad.touchY - s.startY) > kMoveThresh) s.moved = true;
            if (pad.touchSecond) {
                s.remWheel += (s.y - pad.touchY) * kWheelScale;
                int32_t w = static_cast<int32_t>(s.remWheel);
                s.remWheel -= static_cast<float>(w);
                io.wheel += w;
                if (w != 0) s.moved = true;
            } else {
                s.remX += (pad.touchX - s.x) * kScaleX;
                s.remY += (pad.touchY - s.y) * kScaleY;
                int32_t dx = static_cast<int32_t>(s.remX), dy = static_cast<int32_t>(s.remY);
                s.remX -= static_cast<float>(dx);
                s.remY -= static_cast<float>(dy);
                io.dx += dx;
                io.dy += dy;
            }
            s.x = pad.touchX;
            s.y = pad.touchY;
            if (pad.btnTouchpad && !pad.btnOptions) {
                if (!s.dragging && s.moved) {
                    s.dragging = true;
                    s.dragButton = (s.hadSecond || pad.touchX >= 0.65f) ? 2 : 1;
                } else if (!s.dragging) {
                    s.downMs = 0;  // plain click without moving first: controller View
                }
            } else {
                s.dragging = false;
                s.dragButton = 0;
            }
        }
    } else if (s.touching) {
        if (!s.moved && s.downMs > 0 && nowMs >= s.downMs && (nowMs - s.downMs) <= kTapMaxMs && !pad.btnTouchpad) {
            s.tapButton = s.hadSecond ? 2 : 1;
            s.tapUntilMs = nowMs + kTapHoldMs;
        }
        s.touching = s.second = s.hadSecond = s.moved = s.dragging = false;
        s.dragButton = 0;
        s.downMs = 0;
    }

    uint8_t buttons = 0;
    if (s.dragging) buttons |= s.dragButton;
    if (nowMs < s.tapUntilMs) buttons |= s.tapButton;
    else s.tapButton = 0;
    io.buttons |= buttons;
    if (io.dx != 0 || io.dy != 0 || io.wheel != 0 || buttons != s.prevButtons) io.moved = true;
    s.prevButtons = buttons;
    return s.dragging;
}

}  // namespace xc::input
