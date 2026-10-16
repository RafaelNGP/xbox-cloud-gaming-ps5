// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// What the desktop's SDL3 input backends (controller_sdl.cpp, kbm_sdl.cpp)
// work out, kept free of SDL so the tests check it: SDL's numbers are
// fixed by its ABI and written out here.
#pragma once

#include "input/tuning.h"

#include <cstddef>
#include <cstdint>

namespace xc::input {

// A stick axis (SDL: -32768..32767, +y down) -> -1..1.
float sdlStickAxis(int16_t v);
// A trigger (SDL: 0..32767) -> 0..1.
float sdlTriggerAxis(int16_t v);
// The accelerometer: SDL reads m/s^2, gyro aiming takes g.
constexpr float kStandardGravity = 9.80665f;  // SDL_STANDARD_GRAVITY
void sdlAccelToG(const float ms2[3], float g[3]);

// SDL_Scancode values are USB HID usages (keyboard page): the key's
// Windows virtual-key code (kbm.h), the modifiers (224..231: left ctrl,
// shift, alt, GUI, then the right ones) as the PS5's modifier bits; 0 for
// none.
uint8_t scancodeToVk(int scancode);

// SDL's mouse button mask (left 1, middle 2, right 4, X1 8, X2 16) -> the
// app's and the wire's (kbm.h: 1 left, 2 right, 4 middle, 8 back, 16 forward).
uint8_t sdlMouseButtons(uint32_t sdlMask);

// The light bar of player `slot` (0..3) while the app sets none: the
// player's colour (ui::playerColor: blue, red, green, pink).
void playerLightColour(int slot, uint8_t rgb[3]);

// The DualSense's effects packet for SDL_SendGamepadEffect (its output
// report from the enable bits on, as SDL's DS5EffectsState_t): only the two
// triggers' effects enabled (byte 0: 0x04 right, 0x08 left), the right one's
// 11-byte block at byte 10, the left one's at 21; rumble and lights left as
// they are.
constexpr size_t kDs5EffectsSize = 47;
constexpr size_t kDs5RightTrigger = 10, kDs5LeftTrigger = 21, kDs5TriggerBlock = 11;
// One trigger's block for a command (tuning.h): mode 0 -> off (0x05);
// 1, feedback -> 0x21, the zones from its position to 9 as a 16-bit mask
// and 3 bits of (strength - 1) per zone; 3, vibration -> 0x26, the zones,
// (amplitude - 1) per zone, the frequency in byte 9. A zero strength,
// amplitude or frequency is off too.
void ds5TriggerBlock(const TriggerCommand& c, uint8_t out[kDs5TriggerBlock]);
void ds5TriggerEffects(const TriggerCommand& left, const TriggerCommand& right, uint8_t out[kDs5EffectsSize]);

}  // namespace xc::input
