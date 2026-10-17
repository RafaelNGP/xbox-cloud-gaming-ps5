// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The DualSense through libScePad, one per signed-in user (controller.h):
// the pads opened as users sign in and out, read, and sent the effects the
// shared code (controller_common.cpp) works out. controller_sdl.cpp and
// controller_null.cpp stand in elsewhere.
#include "input/controller.h"
#include "input/controller_backend.h"
#include "input/input_ext.h"
#include "util/log.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

extern "C" {
// Layout of OpenOrbis's OrbisPadData, as the console fills it (connected
// sits at offset 76).
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
    int32_t userId[4];  // -1: none
};

int sceUserServiceInitialize(const SceUserServiceInitializeParams* param);
int sceUserServiceGetInitialUser(int32_t* userId);
int sceUserServiceGetLoginUserIdList(SceUserServiceLoginUserIdList* list);
int sceUserServiceGetUserName(int32_t userId, char* name, size_t size);
int scePadInit(void);
int scePadOpen(int32_t userId, int32_t type, int32_t index, const void* param);
int scePadClose(int32_t handle);
int scePadReadState(int32_t handle, ScePadData* data);
int scePadSetMotionSensorState(int32_t handle, bool enable);

struct ScePadVibrationParam {
    uint8_t largeMotor;
    uint8_t smallMotor;
};
int scePadSetVibration(int32_t handle, const ScePadVibrationParam* param);
int scePadSetVibrationMode(int32_t handle, int32_t mode);

// Adaptive triggers (layout as ProsperoEden's ps5_pad.hpp).
struct ScePadTriggerEffectCommand {
    int32_t mode;  // 0 off, 3 vibration
    int32_t reserved;
    uint8_t data[48];  // vibration: position (0..9), amplitude (0..8), frequency (Hz)
};
struct ScePadTriggerEffectParam {
    uint8_t triggerMask;  // 1 = L2, 2 = R2
    uint8_t reserved[7];
    ScePadTriggerEffectCommand command[2];
};
static_assert(sizeof(ScePadTriggerEffectParam) == 120, "ScePadTriggerEffectParam layout");
int scePadSetTriggerEffect(int32_t handle, const ScePadTriggerEffectParam* param);
struct ScePadColor {
    uint8_t r, g, b, reserved;
};
int scePadSetLightBar(int32_t handle, const ScePadColor* color);
int scePadResetLightBar(int32_t handle);
int scePadSetVibrationTriggerEffectWeakWhileEmbeddedMicInUse(int32_t handle, bool enable);
}

namespace xc::input {

namespace {

// A signed-in user's pad.
struct Slot {
    int32_t userId = -1;
    int32_t handle = -1;
    char name[32] = {};  // the user's, read when opened
};
Slot g_slots[kMaxPads];
bool g_rumbleLogged = false;

inline float normStick(uint8_t val) {
    // 0..255 -> -1.0 .. 1.0
    return std::clamp((static_cast<float>(val) - 128.0f) / 128.0f, -1.0f, 1.0f);
}

inline float normTrigger(uint8_t val) {
    return static_cast<float>(val) / 255.0f;
}

bool openPad(int slot, int32_t userId) {
    Slot& pad = g_slots[slot];
    int32_t h = scePadOpen(userId, 0, 0, nullptr);
    if (h < 0) {
        XC_LOGW("scePadOpen(user %d): 0x%08x", userId, static_cast<unsigned>(h));
        return false;
    }
    // The DualSense starts in haptics mode, where scePadSetVibration succeeds
    // and does nothing; mode 2 is classic two-motor rumble (as ProsperoEden).
    int mode = scePadSetVibrationMode(h, 2);
    // The motion sensors (gyro aiming) report only once turned on.
    if (int rc = scePadSetMotionSensorState(h, true); rc < 0)
        XC_LOGW("scePadSetMotionSensorState: 0x%08x", static_cast<unsigned>(rc));
    pad.userId = userId;
    pad.handle = h;
    if (sceUserServiceGetUserName(userId, pad.name, sizeof pad.name) != 0) pad.name[0] = 0;
    slotOpened(slot);
    XC_LOGI("pad %d opened (handle %d, rumble mode 0x%08x)", slot, h, static_cast<unsigned>(mode));
    return true;
}

void closePad(int slot) {
    Slot& pad = g_slots[slot];
    if (pad.handle < 0) return;
    ScePadVibrationParam off{0, 0};
    scePadSetVibration(pad.handle, &off);
    ScePadTriggerEffectParam none{};
    none.triggerMask = 3;
    scePadSetTriggerEffect(pad.handle, &none);
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
    ScePadData pad{};
    int rc = scePadReadState(g_slots[slot].handle, &pad);
    if (rc != 0 || !pad.connected) return false;
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
    out.leftX = normStick(pad.leftStick.x);
    out.leftY = normStick(pad.leftStick.y);
    out.rightX = normStick(pad.rightStick.x);
    out.rightY = normStick(pad.rightStick.y);
    out.l2 = normTrigger(pad.l2);
    out.r2 = normTrigger(pad.r2);

    std::copy(pad.angularVelocity, pad.angularVelocity + 3, out.gyro);
    std::copy(pad.acceleration, pad.acceleration + 3, out.accel);
    out.hasAccel = true;

    // The DualSense touchpad reports 0..1919 x 0..1079.
    if (pad.touchData.touchNum > 0) {
        out.touching = true;
        out.touchX = std::clamp(pad.touchData.touch[0].x / 1919.0f, 0.0f, 1.0f);
        out.touchY = std::clamp(pad.touchData.touch[0].y / 1079.0f, 0.0f, 1.0f);
    }
    return true;
}

void setMotors(int slot, uint8_t large, uint8_t small, uint32_t) {
    ScePadVibrationParam p{large, small};
    int rc = scePadSetVibration(g_slots[slot].handle, &p);
    if (!g_rumbleLogged || rc != 0) XC_LOGI("scePadSetVibration(%u, %u): 0x%08x", p.largeMotor, p.smallMotor, rc);
    g_rumbleLogged = true;
}

TriggerStyle triggerStyle(int) { return TriggerStyle::Adaptive; }

int setTriggerEffects(int slot, const TriggerCommand command[2]) {
    ScePadTriggerEffectParam p{};
    p.triggerMask = 3;
    for (int t = 0; t < 2; ++t) {
        p.command[t].mode = command[t].mode;
        std::copy(command[t].data, command[t].data + 4, p.command[t].data);
    }
    return scePadSetTriggerEffect(g_slots[slot].handle, &p);
}

void setTriggerMotors(int, uint8_t, uint8_t, uint32_t) {}  // the DualSense's are its effects

void setLight(int slot, uint8_t r, uint8_t g, uint8_t b) {
    ScePadColor c{r, g, b, 0};
    int rc = scePadSetLightBar(g_slots[slot].handle, &c);
    static bool logged = false;
    if (!logged || rc != 0) XC_LOGI("scePadSetLightBar(%u, %u, %u): 0x%08x", c.r, c.g, c.b, static_cast<unsigned>(rc));
    logged = true;
}

void resetLight(int slot) { scePadResetLightBar(g_slots[slot].handle); }

}  // namespace backend

std::string padUserName(int index) {
    // Main thread, like refreshPads() which changes it.
    return index >= 0 && index < kMaxPads && g_slots[index].handle >= 0 ? g_slots[index].name : "";
}

std::string padDeviceName(int) { return {}; }  // the console's pads go by their user (padUserName)

int32_t padUserId(int index) {
    if (index >= 0 && index < kMaxPads && g_slots[index].handle >= 0 && g_slots[index].userId >= 0) {
        return g_slots[index].userId;
    }
    int32_t uid = -1;
    if (sceUserServiceGetInitialUser(&uid) == 0 && uid >= 0) {
        return uid;
    }
    return -1;
}

// Every pad here is a DualSense: the UI keeps every option, as before.
PadCaps padCaps(int index) {
    if (!padConnected(index)) return {};
    return {PadKind::DualSense, true, true, true, true, true, true};
}

PadCaps anyPadCaps() { return {PadKind::DualSense, true, true, true, true, true, true}; }

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
    if (!openPad(0, uid)) return false;
    refreshPads();
    return true;
}

void refreshPads() {
    SceUserServiceLoginUserIdList list;
    std::memset(&list, 0xFF, sizeof list);
    if (sceUserServiceGetLoginUserIdList(&list) != 0) return;
    auto signedIn = [&](int32_t user) {
        for (int32_t u : list.userId)
            if (u == user) return true;
        return false;
    };
    // Signed out: free the slot (pad 0 stays with the app's user).
    for (int i = 1; i < kMaxPads; ++i)
        if (g_slots[i].handle >= 0 && !signedIn(g_slots[i].userId)) closePad(i);
    // Signed in: the first free slot.
    for (int32_t user : list.userId) {
        if (user < 0) continue;
        bool known = false;
        for (const Slot& p : g_slots) known |= p.userId == user;
        if (known) continue;
        for (int i = 1; i < kMaxPads; ++i)
            if (g_slots[i].handle < 0) {
                openPad(i, user);
                break;
            }
    }
}

void shutdown() {
    for (int i = 0; i < kMaxPads; ++i) closePad(i);
}

bool poll(ControllerState& out) { return pollPad(0, out); }

void setEmbeddedMicActive(bool active, int pad) {
    if (pad < 0 || pad >= kMaxPads) return;
    int32_t handle = g_slots[pad].handle;
    if (handle >= 0) {
        int rc = scePadSetVibrationTriggerEffectWeakWhileEmbeddedMicInUse(handle, active);
        if (rc < 0) {
            XC_LOGW("scePadSetVibrationTriggerEffectWeakWhileEmbeddedMicInUse: 0x%08x", static_cast<unsigned>(rc));
        } else {
            XC_LOGI("pad %d embedded mic vibration suppression: %s", pad, active ? "enabled" : "disabled");
        }
    }
}

} // namespace xc::input
