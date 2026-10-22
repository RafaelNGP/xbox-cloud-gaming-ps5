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

namespace xc::input {

namespace {

std::mutex g_usbPadMutex;
bool g_initialized = false;
ControllerState g_lastUsbState{};
uint64_t g_lastPacketMs = 0;

std::vector<uint8_t> g_testReport;
bool g_hasTestReport = false;

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
            }
        }
        if (g_uhidFds[i] < 0) {
            char path[32];
            std::snprintf(path, sizeof(path), "/dev/uhid%d", i);
            int fd = open(path, O_RDONLY | O_NONBLOCK);
            if (fd >= 0) {
                XC_LOGI("usb pad: opened %s (fd %d)", path, fd);
                g_uhidFds[i] = fd;
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

void padUsbInjectReportForTesting(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(g_usbPadMutex);
    if (!data || len == 0) {
        g_hasTestReport = false;
        g_testReport.clear();
        return;
    }
    g_testReport.assign(data, data + len);
    g_hasTestReport = true;
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
}

bool pollUsbPad(ControllerState& out) {
    std::lock_guard<std::mutex> lock(g_usbPadMutex);

    if (g_hasTestReport) {
        ControllerState s{};
        if (parse8BitDoDInputReport(g_testReport.data(), g_testReport.size(), s)) {
            g_lastUsbState = s;
            g_lastPacketMs = platform::nowMs();
            out = s;
            return true;
        }
    }

#if XC_HAS_UHID_POSIX
    if (!g_initialized) {
        g_initialized = true;
    }
    scanUhidDevices();

    uint8_t buffer[64];
    bool gotNew = false;
    for (int i = 0; i < 8; ++i) {
        int fd = g_uhidFds[i];
        if (fd < 0) continue;
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n >= 16) {
            ControllerState s{};
            if (parse8BitDoDInputReport(buffer, static_cast<size_t>(n), s)) {
                g_lastUsbState = s;
                g_lastPacketMs = platform::nowMs();
                gotNew = true;
            }
        } else if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                close(fd);
                g_uhidFds[i] = -1;
            }
        }
    }

    uint64_t now = platform::nowMs();
    if (gotNew || (g_lastUsbState.connected && (now - g_lastPacketMs < 500))) {
        out = g_lastUsbState;
        return true;
    }
#endif

    out = ControllerState{};
    return false;
}

}  // namespace xc::input
