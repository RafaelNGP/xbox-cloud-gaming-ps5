// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include <cstdint>

namespace xc::input {

struct ControllerState {
    bool connected = false;

    // Digital Buttons
    bool dpadUp = false;
    bool dpadDown = false;
    bool dpadLeft = false;
    bool dpadRight = false;

    bool btnA = false; // Cross on DualSense
    bool btnB = false; // Circle on DualSense
    bool btnX = false; // Square on DualSense
    bool btnY = false; // Triangle on DualSense

    bool btnL1 = false;
    bool btnR1 = false;
    bool btnL3 = false; // Left stick click
    bool btnR3 = false; // Right stick click

    bool btnOptions = false; // Menu
    bool btnTouchpad = false; // View / Select

    // Analog axes [-1.0 .. 1.0]
    float leftStickX = 0.0f;
    float leftStickY = 0.0f;
    float rightStickX = 0.0f;
    float rightStickY = 0.0f;

    // Triggers [0.0 .. 1.0]
    float triggerL2 = 0.0f;
    float triggerR2 = 0.0f;
};

bool init();
void shutdown();
bool poll(ControllerState& out);

} // namespace xc::input
