// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// USB HID driver for 8BitDo Ultimate 2.4G wireless adapter on PS5 /dev/uhid* in D-Input mode.
#include "input/pad_usb.h"
#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__) || defined(__posix)
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define XC_HAS_UHID_POSIX 1
#else
#define XC_HAS_UHID_POSIX 0
#endif

#include "input/keyboard_nav.h"

namespace xc::input {

namespace {

std::mutex g_usbPadMutex;
bool g_initialized = false;
ControllerState g_lastUsbState{};
uint64_t g_lastPacketMs = 0;
std::array<ControllerState, 8> g_slotStates{};
std::array<uint64_t, 8> g_slotPacketMs{};

std::vector<uint8_t> g_testReport;
bool g_hasTestReport = false;
std::array<std::vector<uint8_t>, 8> g_slotTestReports;
std::array<bool, 8> g_hasSlotTestReport{};

#if XC_HAS_UHID_POSIX
std::array<int, 8> g_uhidFds = {-1, -1, -1, -1, -1, -1, -1, -1};
uint64_t g_lastScanMs = 0;

void scanUhidDevices() {
    uint64_t now = platform::nowMs();
    if (now - g_lastScanMs < 2000) {
        return;
    }
    g_lastScanMs = now;

    // Check existing fds and scan for unopened nodes
    for (int i = 0; i < 8; ++i) {
        if (g_uhidFds[i] >= 0) {
            struct stat st{};
            if (fstat(g_uhidFds[i], &st) < 0) {
                close(g_uhidFds[i]);
                g_uhidFds[i] = -1;
                g_slotStates[i] = ControllerState{};
                g_slotPacketMs[i] = 0;
            }
        }
        if (g_uhidFds[i] < 0) {
            char path[32];
            std::snprintf(path, sizeof(path), "/dev/uhid%d", i);
            int fd = open(path, O_RDONLY | O_NONBLOCK);
            if (fd >= 0) {
                XC_LOGI("usb pad: opened %s (fd %d)", path, fd);
                g_uhidFds[i] = fd;
                g_slotStates[i] = ControllerState{};
                g_slotPacketMs[i] = 0;
            }
        }
    }
}
#endif

}  // namespace

bool parse8BitDoDInputReport(const uint8_t* data, size_t len, ControllerState& out) {
    if (!data || len < 16) {
        return false;
    }

    out = ControllerState{};
    out.connected = true;

    // Analog sticks: 0..255 (128 center) -> [-1.0 .. 1.0]
    out.rawLeftX = std::clamp((static_cast<float>(data[0]) - 128.0f) / 128.0f, -1.0f, 1.0f);
    out.rawLeftY = std::clamp((static_cast<float>(data[1]) - 128.0f) / 128.0f, -1.0f, 1.0f);
    out.rawRightX = std::clamp((static_cast<float>(data[2]) - 128.0f) / 128.0f, -1.0f, 1.0f);
    out.rawRightY = std::clamp((static_cast<float>(data[3]) - 128.0f) / 128.0f, -1.0f, 1.0f);

    out.leftStickX = std::abs(out.rawLeftX) < 0.05f ? 0.0f : out.rawLeftX;
    out.leftStickY = std::abs(out.rawLeftY) < 0.05f ? 0.0f : out.rawLeftY;
    out.rightStickX = std::abs(out.rawRightX) < 0.05f ? 0.0f : out.rawRightX;
    out.rightStickY = std::abs(out.rawRightY) < 0.05f ? 0.0f : out.rawRightY;

    // Triggers L2/R2: 0..255 -> [0.0 .. 1.0]
    out.triggerL2 = std::clamp(static_cast<float>(data[4]) / 255.0f, 0.0f, 1.0f);
    out.triggerR2 = std::clamp(static_cast<float>(data[5]) / 255.0f, 0.0f, 1.0f);

    // D-Pad: data[6] (Hat switch: 0..7 directions, 8 / 0x0F = neutral)
    uint8_t hat = data[6] & 0x0F;
    if (hat <= 7) {
        out.dpadUp = (hat == 7 || hat == 0 || hat == 1);
        out.dpadRight = (hat == 1 || hat == 2 || hat == 3);
        out.dpadDown = (hat == 3 || hat == 4 || hat == 5);
        out.dpadLeft = (hat == 5 || hat == 6 || hat == 7);
    } else {
        out.dpadUp = false;
        out.dpadRight = false;
        out.dpadDown = false;
        out.dpadLeft = false;
    }

    // Action buttons: data[7]
    out.btnA = (data[7] & 0x01) != 0;
    out.btnB = (data[7] & 0x02) != 0;
    out.btnX = (data[7] & 0x04) != 0;
    out.btnY = (data[7] & 0x08) != 0;
    out.btnL1 = (data[7] & 0x10) != 0;
    out.btnR1 = (data[7] & 0x20) != 0;
    if ((data[7] & 0x40) != 0) out.triggerL2 = 1.0f;
    if ((data[7] & 0x80) != 0) out.triggerR2 = 1.0f;

    // System buttons: data[8]
    out.btnTouchpad = (data[8] & 0x01) != 0;  // Select / View
    out.btnOptions = (data[8] & 0x02) != 0;   // Start / Menu
    out.btnNexus = (data[8] & 0x04) != 0;     // Home / Xbox Guide
    out.btnL3 = (data[8] & 0x08) != 0;
    out.btnR3 = (data[8] & 0x10) != 0;

    return true;
}

bool parseXbox360Report(const uint8_t* data, size_t len, ControllerState& out) {
    if (!data || len < 12) return false;

    size_t off = 0;
    if (len >= 20 && data[0] == 0x00 && data[1] == 0x14) {
        off = 2;
    } else if (len >= 18 && data[0] == 0x00 && data[3] == 0x14) {
        off = 4;
    } else if (len >= 14 && data[0] == 0x00 && (data[1] == 0x14 || data[1] == 0x00)) {
        off = 2;
    }

    if (len < off + 12) return false;

    out = ControllerState{};
    out.connected = true;

    uint8_t b1 = data[off];
    out.dpadUp = (b1 & 0x01) != 0;
    out.dpadDown = (b1 & 0x02) != 0;
    out.dpadLeft = (b1 & 0x04) != 0;
    out.dpadRight = (b1 & 0x08) != 0;
    out.btnOptions = (b1 & 0x10) != 0;   // Start
    out.btnTouchpad = (b1 & 0x20) != 0;  // Back / View
    out.btnL3 = (b1 & 0x40) != 0;
    out.btnR3 = (b1 & 0x80) != 0;

    uint8_t b2 = data[off + 1];
    out.btnL1 = (b2 & 0x01) != 0;
    out.btnR1 = (b2 & 0x02) != 0;
    out.btnNexus = (b2 & 0x04) != 0;  // Guide
    out.btnA = (b2 & 0x10) != 0;
    out.btnB = (b2 & 0x20) != 0;
    out.btnX = (b2 & 0x40) != 0;
    out.btnY = (b2 & 0x80) != 0;

    out.triggerL2 = std::clamp(static_cast<float>(data[off + 2]) / 255.0f, 0.0f, 1.0f);
    out.triggerR2 = std::clamp(static_cast<float>(data[off + 3]) / 255.0f, 0.0f, 1.0f);

    int16_t lx = static_cast<int16_t>(data[off + 4] | (data[off + 5] << 8));
    int16_t ly = static_cast<int16_t>(data[off + 6] | (data[off + 7] << 8));
    int16_t rx = static_cast<int16_t>(data[off + 8] | (data[off + 9] << 8));
    int16_t ry = static_cast<int16_t>(data[off + 10] | (data[off + 11] << 8));

    out.rawLeftX = std::clamp(static_cast<float>(lx) / 32768.0f, -1.0f, 1.0f);
    out.rawLeftY = std::clamp(-static_cast<float>(ly) / 32768.0f, -1.0f, 1.0f);
    out.rawRightX = std::clamp(static_cast<float>(rx) / 32768.0f, -1.0f, 1.0f);
    out.rawRightY = std::clamp(-static_cast<float>(ry) / 32768.0f, -1.0f, 1.0f);

    out.leftStickX = std::abs(out.rawLeftX) < 0.05f ? 0.0f : out.rawLeftX;
    out.leftStickY = std::abs(out.rawLeftY) < 0.05f ? 0.0f : out.rawLeftY;
    out.rightStickX = std::abs(out.rawRightX) < 0.05f ? 0.0f : out.rawRightX;
    out.rightStickY = std::abs(out.rawRightY) < 0.05f ? 0.0f : out.rawRightY;

    return true;
}

bool parseXboxOneReport(const uint8_t* data, size_t len, ControllerState& out) {
    if (!data || len < 18) return false;

    // Standard GIP report starts with 0x20
    size_t off = 0;
    if (data[0] == 0x20) {
        off = 4;
    }
    if (len < off + 14) return false;

    out = ControllerState{};
    out.connected = true;

    uint8_t b1 = data[off];
    out.btnOptions = (b1 & 0x04) != 0;   // Menu
    out.btnTouchpad = (b1 & 0x08) != 0;  // View
    out.btnA = (b1 & 0x10) != 0;
    out.btnB = (b1 & 0x20) != 0;
    out.btnX = (b1 & 0x40) != 0;
    out.btnY = (b1 & 0x80) != 0;

    uint8_t b2 = data[off + 1];
    out.dpadUp = (b2 & 0x01) != 0;
    out.dpadDown = (b2 & 0x02) != 0;
    out.dpadLeft = (b2 & 0x04) != 0;
    out.dpadRight = (b2 & 0x08) != 0;
    out.btnL1 = (b2 & 0x10) != 0;
    out.btnR1 = (b2 & 0x20) != 0;
    out.btnL3 = (b2 & 0x40) != 0;
    out.btnR3 = (b2 & 0x80) != 0;

    uint16_t trigL = static_cast<uint16_t>(data[off + 2] | (data[off + 3] << 8));
    uint16_t trigR = static_cast<uint16_t>(data[off + 4] | (data[off + 5] << 8));
    out.triggerL2 = std::clamp(static_cast<float>(trigL) / 1023.0f, 0.0f, 1.0f);
    out.triggerR2 = std::clamp(static_cast<float>(trigR) / 1023.0f, 0.0f, 1.0f);

    int16_t lx = static_cast<int16_t>(data[off + 6] | (data[off + 7] << 8));
    int16_t ly = static_cast<int16_t>(data[off + 8] | (data[off + 9] << 8));
    int16_t rx = static_cast<int16_t>(data[off + 10] | (data[off + 11] << 8));
    int16_t ry = static_cast<int16_t>(data[off + 12] | (data[off + 13] << 8));

    out.rawLeftX = std::clamp(static_cast<float>(lx) / 32768.0f, -1.0f, 1.0f);
    out.rawLeftY = std::clamp(-static_cast<float>(ly) / 32768.0f, -1.0f, 1.0f);
    out.rawRightX = std::clamp(static_cast<float>(rx) / 32768.0f, -1.0f, 1.0f);
    out.rawRightY = std::clamp(-static_cast<float>(ry) / 32768.0f, -1.0f, 1.0f);

    out.leftStickX = std::abs(out.rawLeftX) < 0.05f ? 0.0f : out.rawLeftX;
    out.leftStickY = std::abs(out.rawLeftY) < 0.05f ? 0.0f : out.rawLeftY;
    out.rightStickX = std::abs(out.rawRightX) < 0.05f ? 0.0f : out.rawRightX;
    out.rightStickY = std::abs(out.rawRightY) < 0.05f ? 0.0f : out.rawRightY;

    if (len >= off + 15 && (data[off + 14] & 0x01)) {
        out.btnNexus = true;
    }

    return true;
}

bool parseDualShock4Report(const uint8_t* data, size_t len, ControllerState& out) {
    if (!data || len < 10) return false;

    size_t off = (data[0] == 0x01) ? 1 : 0;
    if (len < off + 9) return false;

    out = ControllerState{};
    out.connected = true;

    out.rawLeftX = std::clamp((static_cast<float>(data[off]) - 128.0f) / 128.0f, -1.0f, 1.0f);
    out.rawLeftY = std::clamp((static_cast<float>(data[off + 1]) - 128.0f) / 128.0f, -1.0f, 1.0f);
    out.rawRightX = std::clamp((static_cast<float>(data[off + 2]) - 128.0f) / 128.0f, -1.0f, 1.0f);
    out.rawRightY = std::clamp((static_cast<float>(data[off + 3]) - 128.0f) / 128.0f, -1.0f, 1.0f);

    out.leftStickX = std::abs(out.rawLeftX) < 0.05f ? 0.0f : out.rawLeftX;
    out.leftStickY = std::abs(out.rawLeftY) < 0.05f ? 0.0f : out.rawLeftY;
    out.rightStickX = std::abs(out.rawRightX) < 0.05f ? 0.0f : out.rawRightX;
    out.rightStickY = std::abs(out.rawRightY) < 0.05f ? 0.0f : out.rawRightY;

    uint8_t hatAndButtons = data[off + 4];
    uint8_t hat = hatAndButtons & 0x0F;
    if (hat <= 7) {
        out.dpadUp = (hat == 7 || hat == 0 || hat == 1);
        out.dpadRight = (hat == 1 || hat == 2 || hat == 3);
        out.dpadDown = (hat == 3 || hat == 4 || hat == 5);
        out.dpadLeft = (hat == 5 || hat == 6 || hat == 7);
    }
    out.btnX = (hatAndButtons & 0x10) != 0;  // Square -> Xbox X
    out.btnA = (hatAndButtons & 0x20) != 0;  // Cross -> Xbox A
    out.btnB = (hatAndButtons & 0x40) != 0;  // Circle -> Xbox B
    out.btnY = (hatAndButtons & 0x80) != 0;  // Triangle -> Xbox Y

    uint8_t b2 = data[off + 5];
    out.btnL1 = (b2 & 0x01) != 0;
    out.btnR1 = (b2 & 0x02) != 0;
    out.btnTouchpad = (b2 & 0x10) != 0;  // Share
    out.btnOptions = (b2 & 0x20) != 0;   // Options
    out.btnL3 = (b2 & 0x40) != 0;
    out.btnR3 = (b2 & 0x80) != 0;

    if (len >= off + 7) {
        uint8_t b3 = data[off + 6];
        out.btnNexus = (b3 & 0x01) != 0;  // PS Button
        if (b3 & 0x02) out.btnTouchpad = true; // Touchpad click
    }

    if (len >= off + 9) {
        out.triggerL2 = std::clamp(static_cast<float>(data[off + 7]) / 255.0f, 0.0f, 1.0f);
        out.triggerR2 = std::clamp(static_cast<float>(data[off + 8]) / 255.0f, 0.0f, 1.0f);
    } else {
        if (b2 & 0x04) out.triggerL2 = 1.0f;
        if (b2 & 0x08) out.triggerR2 = 1.0f;
    }

    return true;
}

bool parseGenericArcadeReport(const uint8_t* data, size_t len, ControllerState& out) {
    if (!data || len < 6) return false;

    out = ControllerState{};
    out.connected = true;

    out.rawLeftX = std::clamp((static_cast<float>(data[0]) - 128.0f) / 128.0f, -1.0f, 1.0f);
    out.rawLeftY = std::clamp((static_cast<float>(data[1]) - 128.0f) / 128.0f, -1.0f, 1.0f);
    out.leftStickX = std::abs(out.rawLeftX) < 0.05f ? 0.0f : out.rawLeftX;
    out.leftStickY = std::abs(out.rawLeftY) < 0.05f ? 0.0f : out.rawLeftY;

    // In arcade sticks, direction stick is often mapped to Hat switch or D-Pad
    if (out.rawLeftY < -0.5f) out.dpadUp = true;
    if (out.rawLeftY > 0.5f) out.dpadDown = true;
    if (out.rawLeftX < -0.5f) out.dpadLeft = true;
    if (out.rawLeftX > 0.5f) out.dpadRight = true;

    // Action buttons (1-8): data[4]
    uint8_t b4 = data[4];
    out.btnA = (b4 & 0x01) != 0;
    out.btnB = (b4 & 0x02) != 0;
    out.btnX = (b4 & 0x04) != 0;
    out.btnY = (b4 & 0x08) != 0;
    out.btnL1 = (b4 & 0x10) != 0;
    out.btnR1 = (b4 & 0x20) != 0;
    if (b4 & 0x40) out.triggerL2 = 1.0f;
    if (b4 & 0x80) out.triggerR2 = 1.0f;

    uint8_t b5 = data[5];
    if ((b5 & 0x01) != 0) out.btnTouchpad = true; // Select / View
    if ((b5 & 0x02) != 0) out.btnOptions = true;  // Start / Menu
    if ((b5 & 0x04) != 0) out.btnL3 = true;
    if ((b5 & 0x08) != 0) out.btnR3 = true;
    if ((b5 & 0x10) != 0) out.btnNexus = true;   // Home

    return true;
}

bool parseUsbPadReport(const uint8_t* data, size_t len, ControllerState& out) {
    if (!data || len == 0) return false;

    if (data[0] == 0x20 && len >= 18) {
        return parseXboxOneReport(data, len, out);
    }
    if (data[0] == 0x00 && len >= 14 && (data[1] == 0x14 || (len >= 18 && data[3] == 0x14))) {
        return parseXbox360Report(data, len, out);
    }
    if (data[0] == 0x01 && len >= 10 && ((data[5] & 0x0F) <= 8)) {
        return parseDualShock4Report(data, len, out);
    }
    if (len >= 16 && (data[6] & 0x0F) <= 8) {
        return parse8BitDoDInputReport(data, len, out);
    }
    if (len >= 6) {
        return parseGenericArcadeReport(data, len, out);
    }
    return false;
}

void padUsbInjectReportForTesting(const uint8_t* data, size_t len) {
    padUsbInjectSlotReportForTesting(0, data, len);
}

void padUsbInjectSlotReportForTesting(int slot, const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(g_usbPadMutex);
    if (slot < 0 || slot >= 8) return;
    if (!data || len == 0) {
        g_hasSlotTestReport[slot] = false;
        g_slotTestReports[slot].clear();
        g_slotStates[slot] = ControllerState{};
        g_slotPacketMs[slot] = 0;
        return;
    }
    g_slotTestReports[slot].assign(data, data + len);
    g_hasSlotTestReport[slot] = true;
    ControllerState s{};
    if (parseUsbPadReport(data, len, s)) {
        s.connected = true;
        g_slotStates[slot] = s;
        g_slotPacketMs[slot] = platform::nowMs();
    }
}

bool padUsbInit() {
    std::lock_guard<std::mutex> lock(g_usbPadMutex);
    if (g_initialized) return true;
    g_initialized = true;
#if XC_HAS_UHID_POSIX
    scanUhidDevices();
#endif
    return true;
}

void padUsbShutdown() {
    std::lock_guard<std::mutex> lock(g_usbPadMutex);
#if XC_HAS_UHID_POSIX
    for (int& fd : g_uhidFds) {
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }
    }
#endif
    g_initialized = false;
    g_lastUsbState = ControllerState{};
    g_hasTestReport = false;
    g_testReport.clear();
    g_slotStates.fill(ControllerState{});
    g_slotPacketMs.fill(0);
    g_hasSlotTestReport.fill(false);
    for (auto& r : g_slotTestReports) r.clear();
}

bool pollUsbPad(ControllerState& out) {
    std::lock_guard<std::mutex> lock(g_usbPadMutex);

    bool anyTestSlot = false;
    for (int i = 0; i < 8; ++i) {
        if (g_hasSlotTestReport[i]) {
            anyTestSlot = true;
            break;
        }
    }
    if (anyTestSlot) {
        ControllerState merged{};
        bool hasActive = false;
        uint64_t now = platform::nowMs();
        for (int i = 0; i < 8; ++i) {
            if (g_hasSlotTestReport[i] && g_slotStates[i].connected) {
                mergePads(merged, g_slotStates[i]);
                hasActive = true;
            }
        }
        if (hasActive) {
            merged.connected = true;
            g_lastUsbState = merged;
            g_lastPacketMs = now;
            out = merged;
            return true;
        }
        return false;
    }

#if XC_HAS_UHID_POSIX
    if (!g_initialized) {
        g_initialized = true;
    }
    scanUhidDevices();

    uint8_t buffer[64];
    bool gotAny = false;
    uint64_t now = platform::nowMs();

    for (int i = 0; i < 8; ++i) {
        int fd = g_uhidFds[i];
        if (fd < 0) continue;
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n >= 6) {
            ControllerState s{};
            if (parseUsbPadReport(buffer, static_cast<size_t>(n), s)) {
                g_slotStates[i] = s;
                g_slotPacketMs[i] = now;
                gotAny = true;
            }
        } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            close(fd);
            g_uhidFds[i] = -1;
            g_slotStates[i] = ControllerState{};
            g_slotPacketMs[i] = 0;
        }
    }

    ControllerState merged{};
    bool hasActive = false;
    for (int i = 0; i < 8; ++i) {
        if (g_slotPacketMs[i] > 0 && (now - g_slotPacketMs[i] < 500) && g_slotStates[i].connected) {
            mergePads(merged, g_slotStates[i]);
            hasActive = true;
        }
    }

    if (gotAny || hasActive) {
        merged.connected = true;
        g_lastUsbState = merged;
        g_lastPacketMs = now;
        out = merged;
        return true;
    }
#endif

    out = ControllerState{};
    return false;
}

}  // namespace xc::input
