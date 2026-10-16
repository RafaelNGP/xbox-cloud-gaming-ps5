// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The app's screens with a keyboard: the keys held (Windows virtual-key
// codes, whatever reads them: the PS5's library, SDL on a desktop) become
// the controller buttons the screens already answer to.
//
//   arrows / W A S D    D-pad              Enter / Space   Cross (select)
//   Esc / Backspace     Circle (back)      Tab             OPTIONS (settings)
//   F / "/"             Triangle (search)  H / X           Square (hide)
//   Q / E               L1 / R1 (tabs)     Z / C           L2 / R2
//   Page Up / Down      right stick (scroll by pages)      R  R3
//   Delete              touchpad (held: sign out)
//
// In a game the keys go to the game; F12 opens and closes the app's menu.
#pragma once

#include "input/controller.h"

#include <cstdint>
#include <vector>

namespace xc::input {

// The key that opens the app's menu during a game (F12).
constexpr uint8_t kVkAppMenu = 0x7B;

// Adds what the keys held press to `pad` (buttons on, sticks pushed); the
// controller's own input stays.
void keyboardToPad(const std::vector<uint8_t>& held, ControllerState& pad);

}  // namespace xc::input
