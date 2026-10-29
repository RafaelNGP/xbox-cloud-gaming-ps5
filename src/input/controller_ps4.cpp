// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/controller.h"
#include "input/controller_backend.h"
#include "input/input_ext.h"
#include "util/log.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

extern "C" {
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
struct SceUserServiceLoginUserIdList {
    int32_t userId[4];
};

int sceUserServiceInitialize(const SceUserServiceInitializeParams* param);
int sceUserServiceGetInitialUser(int32_t* userId);
int sceUserServiceGetLoginUserIdList(SceUserServiceLoginUserIdList* list);
int sceUserServiceGetUserName(int32_t userId, char* name, size_t size);
int scePadInit(void);
int scePadOpen(int32_t userId, int32_t type, int32_t index, const void* param);
int scePadClose(int32_t handle);
int scePadReadState(int32_t handle, ScePadData* data);

struct ScePadVibrationParam {
    uint8_t largeMotor;
    uint8_t smallMotor;
};
int scePadSetVibration(int32_t handle, const ScePadVibrationParam* param);

struct ScePadColor {
    uint8_t r, g, b, reserved;
};
int scePadSetLightBar(int32_t handle, const ScePadColor* param);
int scePadResetLightBar(int32_t handle);
}

namespace xc::input {

namespace {

struct Slot {
    int handle = -1;
    int32_t userId = -1;
    char name[64]{};
};
Slot g_slots[4];
bool g_rumbleLogged = false;

inline float normStick(uint8_t v) {
    return (v - 128) / 127.5f;
}

inline float normTrigger(uint8_t v) {
    return v / 255.0f;
}

void openPad(int slot, int32_t user) {
    auto& pad = g_slots[slot];
    if (pad.handle >= 0 && pad.userId == user) return;
    if (pad.handle >= 0) {
        scePadClose(pad.handle);
        pad.handle = -1;
    }
    pad.userId = user;
    pad.handle = scePadOpen(user, 0, 0, nullptr);
    if (pad.handle < 0) {
        XC_LOGW("pad: scePadOpen for user %d failed: 0x%08x", user, static_cast<unsigned>(pad.handle));
        return;
    }
    sceUserServiceGetUserName(user, pad.name, sizeof(pad.name));
    slotOpened(slot);
    XC_LOGI("pad %d open (user %d \"%s\", handle %d, DualShock 4)", slot, user, pad.name, pad.handle);
}

void closePad(int slot) {
    auto& pad = g_slots[slot];
    if (pad.handle < 0) return;
    if (slotLightSet(slot)) scePadResetLightBar(pad.handle);
    slotClosed(slot);
    scePadClose(pad.handle);
    XC_LOGI("pad %d closed (user %d)", slot, pad.userId);
    pad.handle = -1;
    pad.userId = -1;
    pad.name[0] = 0;
}

}  // namespace

namespace backend {

bool padOpen(int slot) { return g_slots[slot].handle >= 0; }

bool readPad(int slot, RawPad& out) {
    if (g_slots[slot].handle < 0) return false;
    ScePadData pad{};
    int rc = scePadReadState(g_slots[slot].handle, &pad);
    if (rc != 0 || !pad.connected) return false;
    uint32_t b = pad.buttons;

    out.dpadUp    = (b & 0x0010) != 0;
    out.dpadRight = (b & 0x0020) != 0;
    out.dpadDown  = (b & 0x0040) != 0;
    out.dpadLeft  = (b & 0x0080) != 0;

    out.btnA = (b & 0x4000) != 0; // Cross
    out.btnB = (b & 0x2000) != 0; // Circle
    out.btnX = (b & 0x8000) != 0; // Square
    out.btnY = (b & 0x1000) != 0; // Triangle

    out.btnL1 = (b & 0x0400) != 0;
    out.btnR1 = (b & 0x0800) != 0;
    out.btnL3 = (b & 0x0002) != 0;
    out.btnR3 = (b & 0x0004) != 0;

    out.btnOptions  = (b & 0x0008) != 0;
    out.btnTouchpad = (b & 0x100000) != 0;

    out.leftX = normStick(pad.leftStick.x);
    out.leftY = normStick(pad.leftStick.y);
    out.rightX = normStick(pad.rightStick.x);
    out.rightY = normStick(pad.rightStick.y);
    out.l2 = normTrigger(pad.l2);
    out.r2 = normTrigger(pad.r2);

    std::copy(pad.angularVelocity, pad.angularVelocity + 3, out.gyro);
    std::copy(pad.acceleration, pad.acceleration + 3, out.accel);
    out.hasAccel = true;

    // DualShock 4 touchpad: 0..1919 x 0..941
    if (pad.touchData.touchNum > 0) {
        out.touching = true;
        out.touchSecond = pad.touchData.touchNum > 1;
        out.touchX = std::clamp(pad.touchData.touch[0].x / 1919.0f, 0.0f, 1.0f);
        out.touchY = std::clamp(pad.touchData.touch[0].y / 941.0f, 0.0f, 1.0f);
    }
    return true;
}

void setMotors(int slot, uint8_t large, uint8_t small, uint32_t) {
    if (g_slots[slot].handle < 0) return;
    ScePadVibrationParam p{large, small};
    int rc = scePadSetVibration(g_slots[slot].handle, &p);
    if (!g_rumbleLogged || rc != 0) {
        XC_LOGI("scePadSetVibration(%u, %u): 0x%08x", p.largeMotor, p.smallMotor, rc);
    }
    g_rumbleLogged = true;
}

TriggerStyle triggerStyle(int) { return TriggerStyle::None; }
int setTriggerEffects(int, const TriggerCommand[2]) { return 0; }
void setTriggerMotors(int, uint8_t, uint8_t, uint32_t) {}

void setLight(int slot, uint8_t r, uint8_t g, uint8_t b) {
    if (g_slots[slot].handle < 0) return;
    ScePadColor c{r, g, b, 0};
    scePadSetLightBar(g_slots[slot].handle, &c);
}

void resetLight(int slot) {
    if (g_slots[slot].handle >= 0) {
        scePadResetLightBar(g_slots[slot].handle);
    }
}

}  // namespace backend

std::string padUserName(int index) {
    return (index >= 0 && index < 4 && g_slots[index].handle >= 0) ? g_slots[index].name : "";
}

std::string padDeviceName(int) { return {}; }

int32_t padUserId(int index) {
    if (index >= 0 && index < 4 && g_slots[index].handle >= 0 && g_slots[index].userId >= 0) {
        return g_slots[index].userId;
    }
    int32_t uid = -1;
    if (sceUserServiceGetInitialUser(&uid) == 0 && uid >= 0) {
        return uid;
    }
    return -1;
}

PadCaps padCaps(int index) {
    if (!padConnected(index)) return {};
    return {PadKind::DualShock4, true, false, true, true, true, false};
}

PadCaps anyPadCaps() {
    return {PadKind::DualShock4, true, false, true, true, true, false};
}

bool init() {
    int rc = scePadInit();
    if (rc != 0 && rc != static_cast<int>(0x80920002)) {
        XC_LOGE("scePadInit failed: 0x%08x", static_cast<unsigned>(rc));
        return false;
    }
    SceUserServiceInitializeParams param{0x2FF};
    rc = sceUserServiceInitialize(&param);
    if (rc != 0 && rc != static_cast<int>(0x80960003)) {
        XC_LOGW("sceUserServiceInitialize: 0x%08x", static_cast<unsigned>(rc));
    }
    int32_t uid = -1;
    rc = sceUserServiceGetInitialUser(&uid);
    if (rc != 0 || uid < 0) {
        SceUserServiceLoginUserIdList list{};
        if (sceUserServiceGetLoginUserIdList(&list) == 0) {
            for (int32_t u : list.userId) {
                if (u > 0) {
                    uid = u;
                    rc = 0;
                    break;
                }
            }
        }
    }
    if (rc == 0 && uid > 0) {
        openPad(0, uid);
    } else {
        XC_LOGW("sceUserServiceGetInitialUser: 0x%08x (uid=%d), will retry in refreshPads", static_cast<unsigned>(rc), uid);
    }
    refreshPads();
    return true;
}

void refreshPads() {
    SceUserServiceLoginUserIdList list{};
    if (sceUserServiceGetLoginUserIdList(&list) != 0) return;
    auto signedIn = [&](int32_t user) {
        for (int32_t u : list.userId)
            if (u == user) return true;
        return false;
    };

    // If slot 0 is not yet open, try to bind it to the initial user or first active user
    if (g_slots[0].handle < 0) {
        int32_t uid = -1;
        if (sceUserServiceGetInitialUser(&uid) == 0 && uid > 0) {
            openPad(0, uid);
        } else {
            for (int32_t u : list.userId) {
                if (u > 0) {
                    openPad(0, u);
                    break;
                }
            }
        }
    }

    for (int i = 1; i < 4; ++i) {
        if (g_slots[i].handle >= 0 && !signedIn(g_slots[i].userId)) closePad(i);
    }
    for (int32_t user : list.userId) {
        if (user <= 0) continue;
        bool known = false;
        for (const Slot& p : g_slots) known |= (p.userId == user);
        if (known) continue;
        if (g_slots[0].handle < 0) {
            openPad(0, user);
            continue;
        }
        for (int i = 1; i < 4; ++i) {
            if (g_slots[i].handle < 0) {
                openPad(i, user);
                break;
            }
        }
    }
}

void shutdown() {
    for (int i = 0; i < 4; ++i) closePad(i);
}

bool poll(ControllerState& out) {
    if (pollPad(0, out)) return true;
    for (int i = 1; i < 4; ++i) {
        if (g_slots[i].handle >= 0 && pollPad(i, out)) return true;
    }
    return false;
}

void setEmbeddedMicActive(bool /*active*/, int /*pad*/) {}

}  // namespace xc::input
