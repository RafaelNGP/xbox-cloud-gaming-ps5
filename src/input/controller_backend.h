// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Between the controller code every platform shares (controller_common.cpp)
// and a pad backend (controller_ps5.cpp, controller_sdl.cpp,
// controller_null.cpp). The backend opens the pads (its own way: users
// signing in on the PS5, gamepads plugged in on the desktop), reads them and
// sends them effects; the shared code works out the rest once: the rumble
// timeouts, the dead zones, the confirm button swap, the trigger feel, the
// light bar's easing, gyro aiming and when a pad was last used.
// Only the controller backends include this.
#pragma once

#include "input/controller.h"
#include "input/tuning.h"

#include <cstdint>

namespace xc::input {

// One pad as read, before the shared code works on it.
struct RawPad {
    bool dpadUp = false, dpadDown = false, dpadLeft = false, dpadRight = false;
    // Where an Xbox pad has them: A the bottom face button (Cross), B the
    // right one (Circle), X the left (Square), Y the top (Triangle).
    bool btnA = false, btnB = false, btnX = false, btnY = false;
    bool btnL1 = false, btnR1 = false, btnL3 = false, btnR3 = false;
    bool btnOptions = false, btnTouchpad = false, btnNexus = false;
    float leftX = 0, leftY = 0, rightX = 0, rightY = 0;  // -1..1 (+y down), before the dead zone
    float l2 = 0, r2 = 0;                                // 0..1
    float gyro[3] = {0, 0, 0};   // rad/s: x pitch, y yaw, z roll
    float accel[3] = {0, 0, 0};  // g
    bool hasAccel = false;       // accel was read (gyro aiming takes the pad as flat otherwise)
    bool touching = false;
    float touchX = 0, touchY = 0;  // 0..1 from the top left
};

// How a pad's triggers vibrate: a DualSense's effects (resistance, pulses,
// vibration: tuning.h's TriggerCommand), motors of their own (an Xbox pad's
// impulse triggers), or not at all.
enum class TriggerStyle { None, Adaptive, Motors };

namespace backend {

// Whether `slot` (0..kMaxPads-1) has a pad opened.
bool padOpen(int slot);
// Reads it; false when it doesn't answer or isn't connected.
bool readPad(int slot, RawPad& out);
// The two motors, 0..255 (large = low frequency), for `durationMs` more
// (0 = until changed; the shared code sends 0 once the time is up anyway).
void setMotors(int slot, uint8_t large, uint8_t small, uint32_t durationMs);
TriggerStyle triggerStyle(int slot);
// TriggerStyle::Adaptive: both triggers' commands (L2, R2); 0 when the pad
// took them, the error otherwise (the shared code logs it).
int setTriggerEffects(int slot, const TriggerCommand command[2]);
// TriggerStyle::Motors: L2's and R2's motors, 0..255, as setMotors.
void setTriggerMotors(int slot, uint8_t left, uint8_t right, uint32_t durationMs);
// The light bar: a colour, or the system's own again (the player's).
void setLight(int slot, uint8_t r, uint8_t g, uint8_t b);
void resetLight(int slot);

}  // namespace backend

// The backend tells the shared code as a slot's pad comes and goes:
// opened (nothing sent to it yet), about to close (whether the light bar
// shows the app's colour, to give it back) and closed.
void slotOpened(int slot);
bool slotLightSet(int slot);
void slotClosed(int slot);

}  // namespace xc::input
