// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "stream/input_packet.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace xc::stream {

namespace {

constexpr size_t kHeaderSize = 14;

void put8(std::vector<uint8_t>& b, size_t at, uint8_t v) { b[at] = v; }
void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    b[at] = static_cast<uint8_t>(v);
    b[at + 1] = static_cast<uint8_t>(v >> 8);
}
void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}
void put32be(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * (3 - i)));
}

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

std::vector<uint8_t> header(uint16_t type, uint32_t sequence, double timestampMs, size_t total) {
    std::vector<uint8_t> b(total, 0);
    put16(b, 0, type);
    put32(b, 2, sequence);
    uint64_t bits;
    std::memcpy(&bits, &timestampMs, sizeof bits);  // little-endian float64
    for (int i = 0; i < 8; ++i) b[6 + i] = static_cast<uint8_t>(bits >> (8 * i));
    return b;
}

int16_t axis(float v) { return static_cast<int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); }
uint16_t trigger(float v) { return static_cast<uint16_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 65535.0f)); }

}  // namespace

std::vector<uint8_t> clientMetadataReport(uint32_t sequence, double timestampMs, uint8_t maxTouchPoints) {
    auto b = header(kReportClientMetadata, sequence, timestampMs, kHeaderSize + 1);
    put8(b, kHeaderSize, maxTouchPoints);
    return b;
}

std::vector<uint8_t> metadataReport(uint32_t sequence, double timestampMs, const std::vector<FrameMetadata>& frames) {
    size_t count = std::min<size_t>(frames.size(), 30);
    auto b = header(kReportMetadata, sequence, timestampMs, kHeaderSize + 1 + count * 28);
    size_t o = kHeaderSize;
    put8(b, o++, static_cast<uint8_t>(count));
    // Times in tenths of a millisecond, wrapping like the web client's
    // DataView.setUint32.
    auto tenths = [](double ms) { return static_cast<uint32_t>(static_cast<uint64_t>(ms * 10.0)); };
    for (size_t i = frames.size() - count; i < frames.size(); ++i) {
        const FrameMetadata& f = frames[i];
        put32(b, o, f.serverDataKey);
        put32(b, o + 4, tenths(f.firstPacketArrivalMs));
        put32(b, o + 8, tenths(f.submittedMs));
        put32(b, o + 12, tenths(f.decodedMs));
        put32(b, o + 16, tenths(f.renderedMs));
        put32(b, o + 20, tenths(timestampMs));
        put32(b, o + 24, tenths(timestampMs));
        o += 28;
    }
    return b;
}

uint32_t gamepadPhysicality(const GamepadFrame& f) {
    uint32_t p = 0;
    if (f.buttons & kDPadUp) p |= (1 << 0);
    if (f.buttons & kDPadDown) p |= (1 << 1);
    if (f.buttons & kDPadLeft) p |= (1 << 2);
    if (f.buttons & kDPadRight) p |= (1 << 3);
    if (f.buttons & kMenu) p |= (1 << 4);
    if (f.buttons & kView) p |= (1 << 5);
    if (f.buttons & kLeftThumb) p |= (1 << 6);
    if (f.buttons & kRightThumb) p |= (1 << 7);
    if (f.buttons & kLeftShoulder) p |= (1 << 8);
    if (f.buttons & kRightShoulder) p |= (1 << 9);
    if (f.buttons & kNexus) p |= (1 << 10);
    // (1 << 11) is Share/Misc
    if (f.buttons & kA) p |= (1 << 12);
    if (f.buttons & kB) p |= (1 << 13);
    if (f.buttons & kX) p |= (1 << 14);
    if (f.buttons & kY) p |= (1 << 15);
    if (trigger(f.leftTrigger) != 0) p |= (1 << 16);
    if (trigger(f.rightTrigger) != 0) p |= (1 << 17);
    if (axis(f.leftX) != 0) p |= (1 << 18);
    if (axis(f.leftY) != 0) p |= (1 << 19);
    if (axis(f.rightX) != 0) p |= (1 << 20);
    if (axis(f.rightY) != 0) p |= (1 << 21);
    return p;
}

std::vector<uint8_t> gamepadReport(uint32_t sequence, double timestampMs, const GamepadFrame& f) {
    auto b = header(kReportGamepad, sequence, timestampMs, kHeaderSize + 1 + 23);
    size_t o = kHeaderSize;
    put8(b, o++, 1);  // frame count
    put8(b, o++, f.index);
    put16(b, o, f.buttons);
    put16(b, o + 2, static_cast<uint16_t>(axis(f.leftX)));
    put16(b, o + 4, static_cast<uint16_t>(axis(f.leftY)));
    put16(b, o + 6, static_cast<uint16_t>(axis(f.rightX)));
    put16(b, o + 8, static_cast<uint16_t>(axis(f.rightY)));
    put16(b, o + 10, trigger(f.leftTrigger));
    put16(b, o + 12, trigger(f.rightTrigger));
    put32(b, o + 14, gamepadPhysicality(f));  // physical physicality (0 when idle)
    put32(b, o + 18, 0);                      // virtual physicality (LE)
    return b;
}

std::vector<uint8_t> mouseReport(uint32_t sequence, double timestampMs, const std::vector<MouseFrame>& frames) {
    size_t count = std::min<size_t>(frames.size(), 30);
    auto b = header(kReportMouse, sequence, timestampMs, kHeaderSize + 1 + count * 18);
    size_t o = kHeaderSize;
    put8(b, o++, static_cast<uint8_t>(count));
    for (size_t i = 0; i < count; ++i) {
        const MouseFrame& f = frames[i];
        put32(b, o, static_cast<uint32_t>(f.dx));
        put32(b, o + 4, static_cast<uint32_t>(f.dy));
        put32(b, o + 8, static_cast<uint32_t>(f.wheelX));
        put32(b, o + 12, static_cast<uint32_t>(f.wheelY));
        put8(b, o + 16, f.buttons);
        put8(b, o + 17, 0);  // 0: relative movement
        o += 18;
    }
    return b;
}

std::vector<uint8_t> keyboardReport(uint32_t sequence, double timestampMs, const std::vector<KeyFrame>& frames) {
    size_t count = std::min<size_t>(frames.size(), 30);
    auto b = header(kReportKeyboard, sequence, timestampMs, kHeaderSize + 1 + count * 3);
    size_t o = kHeaderSize;
    put8(b, o++, static_cast<uint8_t>(count));
    for (size_t i = 0; i < count; ++i) {
        put8(b, o, 2);  // a virtual-key code (1 known, 2 VKey, 3 app command)
        put8(b, o + 1, frames[i].pressed ? 1 : 0);
        put8(b, o + 2, frames[i].vk);
        o += 3;
    }
    return b;
}

bool parseVibration(const uint8_t* d, size_t len, Vibration& out) {
    if (len < 13 || d[0] != kReportVibration) return false;
    out.gamepadIndex = d[3];
    out.leftMotor = d[4];
    out.rightMotor = d[5];
    out.leftTrigger = d[6];
    out.rightTrigger = d[7];
    out.durationMs = get16(d + 8);
    out.delayMs = get16(d + 10);
    out.repeat = d[12];
    return true;
}

bool parseServerMetadata(const uint8_t* d, size_t len, uint32_t& width, uint32_t& height) {
    if (len < 10 || d[0] != kReportServerMetadata) return false;
    height = get32(d + 2);
    width = get32(d + 6);
    return true;
}

}  // namespace xc::stream
