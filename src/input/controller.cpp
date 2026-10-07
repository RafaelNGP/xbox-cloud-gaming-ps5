// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/controller.h"
#include "util/log.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

#if defined(XCLOUD_PS5)

extern "C" {
// Layout of OpenOrbis's OrbisPadData, as used by the WoW-PS5 port on the
// console (connected sits at offset 76).
struct ScePadStick {
    uint8_t x;
    uint8_t y;
};

struct ScePadTouchPoint {
    uint16_t x;
    uint16_t y;
    uint8_t id;
    uint8_t reserve[3];
};

struct ScePadTouchData {
    uint8_t touchNum;
    uint8_t reserve[3];
    uint32_t reserve1;
    ScePadTouchPoint touch[2];
};

struct ScePadData {
    uint32_t buttons;
    ScePadStick leftStick;
    ScePadStick rightStick;
    uint8_t l2;
    uint8_t r2;
    uint16_t padding;
    float orientation[4];
    float acceleration[3];
    float angularVelocity[3];
    ScePadTouchData touchData;
    uint8_t connected;
    uint64_t timestamp;
    uint8_t ext[16];
    uint8_t count;
    uint8_t unknown[15];
};
static_assert(offsetof(ScePadData, connected) == 76, "ScePadData layout");

struct SceUserServiceInitializeParams {
    uint32_t priority;
};

int sceUserServiceInitialize(const SceUserServiceInitializeParams* param);
int sceUserServiceGetInitialUser(int32_t* userId);
int scePadInit(void);
int scePadOpen(int32_t userId, int32_t type, int32_t index, const void* param);
int scePadClose(int32_t handle);
int scePadReadState(int32_t handle, ScePadData* data);
}

namespace xc::input {

namespace {
int32_t g_userId = -1;
int32_t g_padHandle = -1;

inline float normStick(uint8_t val) {
    // 0..255 -> -1.0 .. 1.0 (deadzone 0.15)
    float v = (static_cast<float>(val) - 128.0f) / 128.0f;
    if (std::abs(v) < 0.15f) return 0.0f;
    return std::clamp(v, -1.0f, 1.0f);
}

inline float normTrigger(uint8_t val) {
    return static_cast<float>(val) / 255.0f;
}
} // namespace

bool init() {
    int rc = scePadInit();
    if (rc != 0) {
        XC_LOGE("scePadInit failed: 0x%08x", rc);
        return false;
    }
    int32_t uid = -1;
    rc = sceUserServiceGetInitialUser(&uid);
    if (rc != 0) {
        SceUserServiceInitializeParams param{0x2FF};  // FIFO lowest
        sceUserServiceInitialize(&param);
        rc = sceUserServiceGetInitialUser(&uid);
    }
    if (rc != 0 || uid < 0) {
        XC_LOGE("sceUserServiceGetInitialUser failed: 0x%08x", rc);
        return false;
    }
    g_userId = uid;
    g_padHandle = scePadOpen(g_userId, 0, 0, nullptr);
    if (g_padHandle < 0) {
        XC_LOGE("scePadOpen failed: 0x%08x", g_padHandle);
        return false;
    }
    XC_LOGI("DualSense pad opened successfully (handle: %d, user: %d)", g_padHandle, g_userId);
    return true;
}

void shutdown() {
    if (g_padHandle >= 0) {
        scePadClose(g_padHandle);
        g_padHandle = -1;
    }
}

bool poll(ControllerState& out) {
    if (g_padHandle < 0) {
        out = {};
        return false;
    }
    ScePadData pad{};
    int rc = scePadReadState(g_padHandle, &pad);
    if (rc != 0 || !pad.connected) {
        out.connected = false;
        return false;
    }

    out.connected = true;
    uint32_t b = pad.buttons;

    // D-Pad
    out.dpadUp    = (b & 0x0010) != 0;
    out.dpadRight = (b & 0x0020) != 0;
    out.dpadDown  = (b & 0x0040) != 0;
    out.dpadLeft  = (b & 0x0080) != 0;

    // DualSense -> Xbox: Cross->A, Circle->B, Square->X, Triangle->Y
    out.btnA = (b & 0x4000) != 0; // Cross
    out.btnB = (b & 0x2000) != 0; // Circle
    out.btnX = (b & 0x8000) != 0; // Square
    out.btnY = (b & 0x1000) != 0; // Triangle

    // Shoulders & Sticks
    out.btnL1 = (b & 0x0400) != 0;
    out.btnR1 = (b & 0x0800) != 0;
    out.btnL3 = (b & 0x0002) != 0;
    out.btnR3 = (b & 0x0004) != 0;

    out.btnOptions  = (b & 0x0008) != 0;
    out.btnTouchpad = (b & 0x100000) != 0;

    // Analog
    out.leftStickX  = normStick(pad.leftStick.x);
    out.leftStickY  = normStick(pad.leftStick.y);
    out.rightStickX = normStick(pad.rightStick.x);
    out.rightStickY = normStick(pad.rightStick.y);

    out.triggerL2 = normTrigger(pad.l2);
    out.triggerR2 = normTrigger(pad.r2);

    return true;
}

} // namespace xc::input

#else

// Host PC fallback implementation
namespace xc::input {
bool init() { return true; }
void shutdown() {}
bool poll(ControllerState& out) {
    out = {};
    out.connected = true;
    return true;
}
} // namespace xc::input

#endif
