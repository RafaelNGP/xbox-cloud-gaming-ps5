// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// USB keyboards and mice on the PS5, for the games that take native mouse
// and keyboard in the cloud (Fortnite and others). The console's keyboard
// library (libSceKeyboard) and mouse library (libSceMouse, loaded at run
// time) are read each frame; keys come out as Windows virtual-key codes,
// which is what the xbox.com client sends.
#pragma once

#include <cstdint>
#include <vector>

namespace xc::input {

// A key that went down or up since the last poll.
struct KeyEvent {
    uint8_t vk = 0;  // Windows virtual-key code
    bool pressed = false;
};

struct KbmInput {
    bool keyboard = false, mouse = false;  // connected
    std::vector<KeyEvent> keys;  // went down / up since the last poll
    std::vector<uint8_t> held;   // held now, sorted
    int32_t dx = 0, dy = 0, wheel = 0;  // mouse movement and wheel steps since the last poll
    uint8_t buttons = 0;                // 1 left, 2 right, 4 middle, 8 back, 16 forward
    bool moved = false;                 // anything to send for the mouse (movement, wheel or buttons changed)
};

// USB HID usage (keyboard page) -> Windows virtual-key code; 0 for none.
uint8_t hidToVk(uint16_t usage);
// The HID modifier bits (left ctrl 0x01 ... right GUI 0x80) -> virtual keys:
// the eight modifier keys as the web client reports them (shift 0x10,
// ctrl 0x11, alt 0x12 for both sides; the Windows keys 0x5B / 0x5C).
uint8_t modifierVk(int bit);

// Loads the libraries; false when neither can be used (it isn't fatal).
bool kbmInit(int32_t userId);
// What happened since the last call. Rescans for devices every few seconds.
KbmInput kbmPoll();

}  // namespace xc::input
