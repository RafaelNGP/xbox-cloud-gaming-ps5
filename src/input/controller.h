// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include <cstdint>
#include <string>

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
    bool btnNexus = false;    // the Xbox button (from the game menu; the PS button stays the system's)

    // Analog axes [-1.0 .. 1.0], after the dead zone
    float leftStickX = 0.0f;
    float leftStickY = 0.0f;
    float rightStickX = 0.0f;
    float rightStickY = 0.0f;
    // The same before it (the Settings stick tester)
    float rawLeftX = 0.0f, rawLeftY = 0.0f, rawRightX = 0.0f, rawRightY = 0.0f;

    // Triggers [0.0 .. 1.0]
    float triggerL2 = 0.0f;
    float triggerR2 = 0.0f;

    // A finger on the touchpad, at (touchX, touchY) in [0, 1] from the top left.
    bool touching = false;
    float touchX = 0.0f, touchY = 0.0f;
};

// Up to four DualSense: pad 0 belongs to the user who started the app, the
// others to the other signed-in users (each PS5 controller is tied to a
// user), in the order they appear.
constexpr int kMaxPads = 4;

bool init();
void shutdown();
// Pad 0 (the menus).
bool poll(ControllerState& out);
// Any pad; false (and `out` cleared) when there is none at `index`.
bool pollPad(int index, ControllerState& out);
// Looks for users who signed in or out since (about once a second).
void refreshPads();
// Whether the pad at `index` answered its last poll, and its user's name
// (empty when the slot is free).
bool padConnected(int index);
std::string padUserName(int index);
int32_t padUserId(int index = 0);
// Rumble, 0..255 per motor (large = low frequency, small = high frequency),
// for `durationMs` (0 = until changed). Any thread; polling applies it.
void setRumble(uint8_t large, uint8_t small, uint32_t durationMs, int pad = 0);
// Vibration in the triggers (Xbox impulse triggers), 0..255 each; the
// DualSense's adaptive triggers vibrate along their whole travel.
void setTriggerRumble(uint8_t left, uint8_t right, uint32_t durationMs, int pad = 0);
// Stick dead zone, 0..0.5 of the travel.
// Each stick's own dead zone (0..1 of the travel): a drifting stick can
// have a bigger one.
void setDeadzone(float left, float right);
// Circle reported as Cross (Xbox A) and Cross as Circle, everywhere.
void setCircleConfirms(bool on);
bool circleConfirms();
// How the triggers feel (input/tuning.h): vibration strength 0 (off:
// setTriggerRumble() is ignored) to kTriggerStrengths - 1, the frequency
// choice (index), the resistance (weight) and the style (pulses of the
// resistance instead of the motor vibrating).
void setTriggerFeel(int strength, int hzIndex, int resistance, bool pulses);
// Logs each time a trigger starts or stops vibrating (the Settings tester).
void setTriggerLogging(bool on);
// The resistance (setTriggerFeel) only weighs the triggers while this is on:
// in a game and in the Settings tester, not while browsing the menus.
void setTriggerResistanceActive(bool on);
// The light bar eases (~0.4 s) to this colour; reset gives it back to the
// system (the player's colour). Any thread; polling applies it.
void setLightBar(uint8_t r, uint8_t g, uint8_t b, int pad = 0);
void resetLightBar(int pad = 0);
// DualSense: suppresses vibration and trigger effect bleed while embedded mic is active
void setEmbeddedMicActive(bool active, int pad = 0);

} // namespace xc::input
