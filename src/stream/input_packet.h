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
    kReportMouse = 32,
    kReportKeyboard = 64,
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

// Mouse movement since the last frame (relative), in the browser's units
// (the web client sends twice PointerEvent.movementX/Y), the wheel's steps
// and the buttons (1 left, 2 right, 4 middle, 8 back, 16 forward).
struct MouseFrame {
    int32_t dx = 0, dy = 0, wheelX = 0, wheelY = 0;
    uint8_t buttons = 0;
};

// A key going down or up, as a Windows virtual-key code (what the web
// client sends from KeyboardEvent.keyCode).
struct KeyFrame {
    uint8_t vk = 0;
    bool pressed = false;
};

struct Vibration {
    uint8_t gamepadIndex = 0;
    // 0..100 percent.
    uint8_t leftMotor = 0, rightMotor = 0, leftTrigger = 0, rightTrigger = 0;
    uint16_t durationMs = 0, delayMs = 0;
    uint8_t repeat = 0;
};

// When one video frame arrived, was decoded and was shown, on the client's
// clock (ms). The server matches it to the frame by `serverDataKey`, the
// frame's RTP timestamp, and measures the stream's latency with it.
struct FrameMetadata {
    uint32_t serverDataKey = 0;
    double firstPacketArrivalMs = 0, submittedMs = 0, decodedMs = 0, renderedMs = 0;
};

// The first report on the channel: announces the client (touch points).
std::vector<uint8_t> clientMetadataReport(uint32_t sequence, double timestampMs, uint8_t maxTouchPoints = 1);
// Frame timings, as the web client sends after each frame it shows (at most
// 30 per report).
std::vector<uint8_t> metadataReport(uint32_t sequence, double timestampMs, const std::vector<FrameMetadata>& frames);
// Bitmask of active controls for a gamepad frame (0 when idle, bits set for
// pressed buttons, engaged triggers and deflected sticks).
uint32_t gamepadPhysicality(const GamepadFrame& frame);
constexpr size_t kGamepadReportSize = 38;
size_t writeGamepadReport(uint8_t* out, size_t outSize, uint32_t sequence, double timestampMs, const GamepadFrame& frame);
std::vector<uint8_t> gamepadReport(uint32_t sequence, double timestampMs, const GamepadFrame& frame);
// Native mouse and keyboard, for the titles that take them (at most 30
// frames per report).
std::vector<uint8_t> mouseReport(uint32_t sequence, double timestampMs, const std::vector<MouseFrame>& frames);
std::vector<uint8_t> keyboardReport(uint32_t sequence, double timestampMs, const std::vector<KeyFrame>& frames);

// Server -> client reports. Return false when `data` is another report type.
bool parseVibration(const uint8_t* data, size_t len, Vibration& out);
bool parseServerMetadata(const uint8_t* data, size_t len, uint32_t& width, uint32_t& height);

}  // namespace xc::stream
