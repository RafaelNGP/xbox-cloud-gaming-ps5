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

constexpr uint16_t kXbox360WiredVid = 0x045E;
constexpr uint16_t kXbox360WiredPid = 0x028E;
constexpr uint16_t kXbox360WirelessVid = 0x045E;
constexpr uint16_t kXbox360WirelessPid1 = 0x0719;
constexpr uint16_t kXbox360WirelessPid2 = 0x0291;

constexpr uint16_t kXboxOneVid = 0x045E;
constexpr uint16_t kXboxOnePid1 = 0x02D1;
constexpr uint16_t kXboxOnePid2 = 0x02DD;
constexpr uint16_t kXboxSeriesPid = 0x0B12;

constexpr uint16_t kDualShock4Vid = 0x054C;
constexpr uint16_t kDualShock4Pid1 = 0x05C4;
constexpr uint16_t kDualShock4Pid2 = 0x09CC;

bool padUsbInit();
void padUsbShutdown();
bool pollUsbPad(ControllerState& out);

// Parses raw 16-byte D-Input report from 8BitDo Ultimate 2.4G USB RF adapter
bool parse8BitDoDInputReport(const uint8_t* data, size_t len, ControllerState& out);

// Parses Xbox 360 USB wired / wireless receiver reports (X-Input 14..20+ bytes)
bool parseXbox360Report(const uint8_t* data, size_t len, ControllerState& out);

// Parses Xbox One / Series USB reports (GIP 0x20 packets)
bool parseXboxOneReport(const uint8_t* data, size_t len, ControllerState& out);

// Parses DualShock 4 USB reports (64 bytes report 0x01)
bool parseDualShock4Report(const uint8_t* data, size_t len, ControllerState& out);

// Parses Generic Standard HID 14/16-button fightsticks and arcade sticks
bool parseGenericArcadeReport(const uint8_t* data, size_t len, ControllerState& out);

// Auto-detects device type and parses any supported USB HID report
bool parseUsbPadReport(const uint8_t* data, size_t len, ControllerState& out);

// Test helper to inject reports in unit tests
void padUsbInjectReportForTesting(const uint8_t* data, size_t len);
void padUsbInjectSlotReportForTesting(int slot, const uint8_t* data, size_t len);

}  // namespace xc::input
