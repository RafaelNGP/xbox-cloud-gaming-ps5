// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Binary reports of the xCloud "input" data channel (protocol "1.0"), as
// written by the xbox.com web client: a 14-byte header (report type, sequence,
// timestamp) followed by the frames the report type announces.
#pragma once

#include <cstdint>
#include <vector>

namespace xc::stream {

enum ReportType : uint16_t {
    kReportMetadata = 1,
    kReportGamepad = 2,
    kReportClientMetadata = 8,
    kReportServerMetadata = 16,
    kReportVibration = 128,
};

// Button bits of a gamepad frame.
enum GamepadButton : uint16_t {
    kNexus = 1 << 1,
    kMenu = 1 << 2,
    kView = 1 << 3,
    kA = 1 << 4,
    kB = 1 << 5,
    kX = 1 << 6,
    kY = 1 << 7,
    kDPadUp = 1 << 8,
    kDPadDown = 1 << 9,
    kDPadLeft = 1 << 10,
    kDPadRight = 1 << 11,
    kLeftShoulder = 1 << 12,
    kRightShoulder = 1 << 13,
    kLeftThumb = 1 << 14,
    kRightThumb = 1 << 15,
};

struct GamepadFrame {
    uint8_t index = 0;
    uint16_t buttons = 0;
    // Sticks in [-1, 1], +Y pointing *up* (the wire format's convention).
    float leftX = 0, leftY = 0, rightX = 0, rightY = 0;
    // Triggers in [0, 1].
    float leftTrigger = 0, rightTrigger = 0;

    bool operator==(const GamepadFrame&) const = default;
};

struct Vibration {
    uint8_t gamepadIndex = 0;
    // 0..100 percent.
    uint8_t leftMotor = 0, rightMotor = 0, leftTrigger = 0, rightTrigger = 0;
    uint16_t durationMs = 0, delayMs = 0;
    uint8_t repeat = 0;
};

// The first report on the channel: announces the client (touch points).
std::vector<uint8_t> clientMetadataReport(uint32_t sequence, double timestampMs, uint8_t maxTouchPoints = 1);
std::vector<uint8_t> gamepadReport(uint32_t sequence, double timestampMs, const GamepadFrame& frame);

// Server -> client reports. Return false when `data` is another report type.
bool parseVibration(const uint8_t* data, size_t len, Vibration& out);
bool parseServerMetadata(const uint8_t* data, size_t len, uint32_t& width, uint32_t& height);

}  // namespace xc::stream
