// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// What every controller backend shares (controller_ps5.cpp, controller_null.cpp).
#include "input/controller.h"

#include <cmath>

namespace xc::input {

bool inUse(const ControllerState& s) {
    return s.btnA || s.btnB || s.btnX || s.btnY || s.dpadUp || s.dpadDown || s.dpadLeft || s.dpadRight || s.btnL1 ||
           s.btnR1 || s.btnL3 || s.btnR3 || s.btnOptions || s.btnTouchpad || s.triggerL2 > 0.5f || s.triggerR2 > 0.5f ||
           std::abs(s.leftStickX) > 0.5f || std::abs(s.leftStickY) > 0.5f || std::abs(s.rightStickX) > 0.5f ||
           std::abs(s.rightStickY) > 0.5f;
}

}  // namespace xc::input
