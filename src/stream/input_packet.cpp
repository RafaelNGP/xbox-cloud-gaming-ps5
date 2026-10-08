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
    put32(b, o + 14, 1);    // physical physicality
    put32be(b, o + 18, 1);  // virtual physicality (big-endian in the web client too)
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
