// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// USB keyboards and mice through libSceKeyboard and libSceMouse (kbm.h).
// kbm_null.cpp stands in elsewhere.
#include "input/kbm.h"

#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <cstddef>
#include <string>

extern "C" {
int sceSysmoduleLoadModule(uint16_t id);
int sceKernelLoadStartModule(const char* path, size_t argc, const void* argv, uint32_t flags, void* opt, int* res);
const char* sceKernelGetFsSandboxRandomWord(void);
int sceKeyboardInit(void);
int sceKeyboardOpen(int32_t user, int32_t type, int32_t index, const void* param);
int sceKeyboardRead(int32_t handle, void* data, int32_t count);
int sceKeyboardClose(int32_t handle);
// libSceMouse: no stub in the SDK (ps5/stubs/libSceMouse.c names them).
int sceMouseInit(void);
int sceMouseOpen(int32_t user, int32_t type, int32_t index, const void* param);
int sceMouseRead(int32_t handle, void* data, int32_t count);
int sceMouseClose(int32_t handle);
}

namespace xc::input {

namespace {

// The keyboard library's state record (ProsperoLight / ProsperoStore
// verified it on a console).
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
static_assert(offsetof(KeyboardState, connected) == 0x10);
static_assert(offsetof(KeyboardState, modifiers) == 0x1c);
static_assert(offsetof(KeyboardState, keys) == 0x20);

// The mouse library's record (as the PS4's).
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

int32_t g_user = -1;
bool g_keyboardReady = false, g_mouseReady = false;
int g_keyboards[kSlots] = {-1, -1}, g_mice[kSlots] = {-1, -1};
uint64_t g_nextScanMs = 0;
std::vector<uint8_t> g_held;  // virtual keys down, sorted
// Each keyboard's keys held at its last record: a read with no new record
// means nothing changed (not that every key went up).
std::vector<uint8_t> g_slotHeld[kSlots];
bool g_slotConnected[kSlots] = {};
uint8_t g_buttons = 0;
bool g_loggedKeyboard = false, g_loggedMouse = false;

std::string systemLib(const char* name) {
    const char* word = sceKernelGetFsSandboxRandomWord();
    return std::string("/") + (word ? word : "") + "/common/lib/" + name;
}

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

}  // namespace

bool kbmInit(int32_t userId) {
    g_user = userId;
    int rc = sceSysmoduleLoadModule(0x0106);  // libSceKeyboard
    if (rc >= 0) rc = sceKeyboardInit();
    g_keyboardReady = rc >= 0;
    XC_LOGI("keyboard library: 0x%08x", static_cast<unsigned>(rc));
    int res = 0;
    int module = sceKernelLoadStartModule(systemLib("libSceMouse.sprx").c_str(), 0, nullptr, 0, nullptr, &res);
    rc = module >= 0 ? sceMouseInit() : module;
    g_mouseReady = rc >= 0;
    XC_LOGI("mouse library: 0x%08x", static_cast<unsigned>(rc));
    rescan(platform::nowMs());  // what is plugged in at the start, in the log
    return g_keyboardReady || g_mouseReady;
}

KbmInput kbmPoll() {
    KbmInput out;
    if (!g_keyboardReady && !g_mouseReady) return out;
    rescan(platform::nowMs());

    // Keyboards: the newest record says what is held now; with none new,
    // what the last one said still holds.
    std::vector<uint8_t> held;
    KeyboardState ks[16];
    for (int i = 0; i < kSlots; ++i) {
        if (g_keyboards[i] < 0) continue;
        int n = sceKeyboardRead(g_keyboards[i], ks, 16);
        if (n < 0) {  // gone: closed, reopened at the next scan
            sceKeyboardClose(g_keyboards[i]);
            g_keyboards[i] = -1;
            g_slotHeld[i].clear();
            g_slotConnected[i] = false;
            continue;
        }
        if (n > 0) {
            const KeyboardState& k = ks[std::min(n, 16) - 1];
            g_slotConnected[i] = k.connected != 0;
            g_slotHeld[i].clear();
            if (k.connected && !k.intercepted) {  // intercepted: the system has it (its own keyboard UI)
                for (int b = 0; b < 8; ++b)
                    if (k.modifiers & (1u << b)) g_slotHeld[i].push_back(modifierVk(b));
                for (int j = 0; j < std::clamp(k.length, 0, 16); ++j)
                    if (uint8_t vk = hidToVk(k.keys[j])) g_slotHeld[i].push_back(vk);
            }
        }
        if (!g_slotConnected[i]) continue;
        out.keyboard = true;
        if (!g_loggedKeyboard) {
            g_loggedKeyboard = true;
            XC_LOGI("keyboard in use");
        }
        held.insert(held.end(), g_slotHeld[i].begin(), g_slotHeld[i].end());
    }
    std::sort(held.begin(), held.end());
    held.erase(std::unique(held.begin(), held.end()), held.end());
    for (uint8_t vk : g_held)
        if (!std::binary_search(held.begin(), held.end(), vk)) out.keys.push_back({vk, false});
    for (uint8_t vk : held)
        if (!std::binary_search(g_held.begin(), g_held.end(), vk)) out.keys.push_back({vk, true});
    g_held.swap(held);
    out.held = g_held;
    static int logged = 0;  // the first few, to check a keyboard works (not what is typed after)
    for (const auto& e : out.keys)
        if (logged < 10) XC_LOGI("keyboard: key 0x%02x %s (%d)", e.vk, e.pressed ? "down" : "up", ++logged);

    // Mice: the movement and wheel add up; the buttons are the newest.
    MouseState ms[32];
    uint8_t buttons = g_buttons;
    bool anyMouse = false;
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
            anyMouse = true;
            out.dx += ms[j].x;
            out.dy += ms[j].y;
            out.wheel += ms[j].wheel;
            buttons = static_cast<uint8_t>(ms[j].buttons & 0x1F);
        }
    }
    if (anyMouse) {
        out.mouse = true;
        if (!g_loggedMouse) {
            g_loggedMouse = true;
            XC_LOGI("mouse in use");
        }
    }
    out.buttons = buttons;
    out.moved = out.dx || out.dy || out.wheel || buttons != g_buttons;
    static int wheelLogged = 0;  // which way the console counts a notch
    if (out.wheel && wheelLogged < 5) XC_LOGI("mouse: wheel %d (%d)", out.wheel, ++wheelLogged);
    static int movesLogged = 0;
    if (out.moved && movesLogged < 10)
        XC_LOGI("mouse: %d,%d wheel %d buttons 0x%02x (%d)", out.dx, out.dy, out.wheel, buttons, ++movesLogged);
    g_buttons = buttons;
    return out;
}

}  // namespace xc::input
