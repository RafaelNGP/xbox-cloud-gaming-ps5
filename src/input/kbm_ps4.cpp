// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/input_ext.h"
#include "input/kbm.h"

#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <cstddef>
#include <vector>

extern "C" {
int sceSysmoduleLoadModule(uint16_t id);
int sceKeyboardInit(void);
int sceKeyboardOpen(int32_t user, int32_t type, int32_t index, const void* param);
int sceKeyboardRead(int32_t handle, void* data, int32_t count);
int sceKeyboardClose(int32_t handle);
int sceMouseInit(void);
int sceMouseOpen(int32_t user, int32_t type, int32_t index, const void* param);
int sceMouseRead(int32_t handle, void* data, int32_t count);
int sceMouseClose(int32_t handle);
}

namespace xc::input {

namespace {

struct KeyboardState {
    uint64_t timestampUs;
    uint8_t intercepted;
    uint8_t reserved0[7];
    uint8_t connected;
    uint8_t reserved1[3];
    int32_t length;  // keys[] in use
    uint32_t leds;
    uint32_t modifiers;  // HID modifier bits
    uint16_t keys[16];   // HID usages held
    uint8_t reserved2[32];
};
static_assert(sizeof(KeyboardState) == 96);

struct MouseState {
    uint64_t timestamp;
    bool connected;
    uint32_t buttons;
    int32_t x, y, wheel, tilt;
    uint8_t reserved[8];
};
static_assert(sizeof(MouseState) == 40);

constexpr int kSlots = 2;
constexpr uint64_t kRescanMs = 3000;
constexpr uint16_t kSysmoduleKeyboard = 0x0106;
constexpr uint16_t kSysmoduleMouse = 0x00A9; // SCE_SYSMODULE_MOUSE on PS4

int32_t g_user = -1;
bool g_keyboardReady = false, g_mouseReady = false;
int g_keyboards[kSlots] = {-1, -1}, g_mice[kSlots] = {-1, -1};
uint64_t g_nextScanMs = 0;
std::vector<uint8_t> g_held;
std::vector<uint8_t> g_slotHeld[kSlots];
bool g_slotConnected[kSlots] = {};
uint8_t g_buttons = 0;
bool g_loggedKeyboard = false, g_loggedMouse = false;

void rescan(uint64_t now) {
    if (now < g_nextScanMs) return;
    g_nextScanMs = now + kRescanMs;
    const uint64_t param = 0;
    for (int i = 0; i < kSlots; ++i) {
        if (g_keyboardReady && g_keyboards[i] < 0) {
            int h = sceKeyboardOpen(g_user, 0, i, &param);
            if (h >= 0) {
                g_keyboards[i] = h;
                XC_LOGI("keyboard %d opened", i + 1);
            }
        }
        if (g_mouseReady && g_mice[i] < 0) {
            int h = sceMouseOpen(g_user, 0, i, &param);
            if (h >= 0) {
                g_mice[i] = h;
                XC_LOGI("mouse %d opened", i + 1);
            }
        }
    }
}

} // namespace

bool kbmInit(int32_t userId) {
    g_user = userId;
    int rc = sceSysmoduleLoadModule(kSysmoduleKeyboard);
    if (rc >= 0) rc = sceKeyboardInit();
    g_keyboardReady = (rc >= 0);
    XC_LOGI("keyboard library: 0x%08x", static_cast<unsigned>(rc));

    rc = sceSysmoduleLoadModule(kSysmoduleMouse);
    if (rc >= 0) rc = sceMouseInit();
    g_mouseReady = (rc >= 0);
    XC_LOGI("mouse library: 0x%08x", static_cast<unsigned>(rc));

    rescan(platform::nowMs());
    return g_keyboardReady || g_mouseReady;
}

KbmInput kbmPoll() {
    KbmInput out;
    if (!g_keyboardReady && !g_mouseReady) return out;
    rescan(platform::nowMs());

    std::vector<uint8_t> held;
    KeyboardState ks[16];
    for (int i = 0; i < kSlots; ++i) {
        if (g_keyboards[i] < 0) continue;
        int n = sceKeyboardRead(g_keyboards[i], ks, 16);
        if (n < 0) {
            sceKeyboardClose(g_keyboards[i]);
            g_keyboards[i] = -1;
            g_slotHeld[i].clear();
            g_slotConnected[i] = false;
            continue;
        }
        if (n > 0) {
            const KeyboardState& k = ks[std::min(n, 16) - 1];
            g_slotConnected[i] = (k.connected != 0);
            g_slotHeld[i].clear();
            if (k.connected && !k.intercepted) {
                for (int b = 0; b < 8; ++b) {
                    if (k.modifiers & (1u << b)) g_slotHeld[i].push_back(modifierVk(b));
                }
                for (int j = 0; j < std::clamp(k.length, 0, 16); ++j) {
                    if (uint8_t vk = hidToVk(k.keys[j])) g_slotHeld[i].push_back(vk);
                }
            }
        }
        if (g_slotConnected[i]) {
            out.keyboard = true;
            held.insert(held.end(), g_slotHeld[i].begin(), g_slotHeld[i].end());
        }
    }

    std::sort(held.begin(), held.end());
    held.erase(std::unique(held.begin(), held.end()), held.end());

    for (uint8_t vk : g_held) {
        if (!std::binary_search(held.begin(), held.end(), vk)) {
            out.keys.push_back({vk, false});
        }
    }
    for (uint8_t vk : held) {
        if (!std::binary_search(g_held.begin(), g_held.end(), vk)) {
            out.keys.push_back({vk, true});
        }
    }
    g_held = held;
    out.held = held;

    if (out.keyboard && !g_loggedKeyboard) {
        g_loggedKeyboard = true;
        XC_LOGI("keyboard in use");
    }

    // Mouse reading
    MouseState ms[32];
    bool mouseMoved = false;
    for (int i = 0; i < kSlots; ++i) {
        if (g_mice[i] < 0) continue;
        int n = sceMouseRead(g_mice[i], ms, 32);
        if (n < 0) {
            sceMouseClose(g_mice[i]);
            g_mice[i] = -1;
            continue;
        }
        for (int j = 0; j < std::min(n, 32); ++j) {
            if (!ms[j].connected) continue;
            out.mouse = true;
            out.dx += ms[j].x;
            out.dy += ms[j].y;
            out.wheel += ms[j].wheel;
            g_buttons = static_cast<uint8_t>(ms[j].buttons & 0x1F);
            mouseMoved = true;
        }
    }

    if (out.mouse && !g_loggedMouse) {
        g_loggedMouse = true;
        XC_LOGI("mouse in use");
    }

    out.buttons = g_buttons;
    out.moved = mouseMoved || (out.dx != 0) || (out.dy != 0) || (out.wheel != 0);

    return out;
}

void kbmSetCaptured(bool /*captured*/) {}

} // namespace xc::input
