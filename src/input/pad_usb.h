// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// USB HID gamepad driver for third-party wireless controllers on PS5 (such as 8BitDo Ultimate 2.4G dongle)
// via /dev/uhid* bypassing Sony's libScePad whitelist.
#pragma once

#include "input/controller.h"

#include <cstddef>
#include <cstdint>

namespace xc::input {

constexpr uint16_t k8BitDoVid = 0x2DC8;
constexpr uint16_t k8BitDoUltimate24GPid = 0x3106;

bool padUsbInit();
void padUsbShutdown();
bool pollUsbPad(ControllerState& out);

// Parses raw 16-byte D-Input report from 8BitDo Ultimate 2.4G USB RF adapter:
// byte 0: Left Stick X (0..255, center 128)
// byte 1: Left Stick Y (0..255, center 128)
// byte 2: Right Stick X (0..255, center 128)
// byte 3: Right Stick Y (0..255, center 128)
// byte 4: Left Trigger L2 (0..255)
// byte 5: Right Trigger R2 (0..255)
// byte 6: D-pad (hat switch 0..7 or bitmask)
// byte 7: Buttons (A, B, X, Y, L1, R1, L2, R2)
// byte 8: System Buttons (View/Select, Menu/Start, Nexus/Home, L3, R3)
// bytes 9..15: telemetry/reserved
bool parse8BitDoDInputReport(const uint8_t* data, size_t len, ControllerState& out);

// Test helper to inject reports in unit tests
void padUsbInjectReportForTesting(const uint8_t* data, size_t len);

}  // namespace xc::input
