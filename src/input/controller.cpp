// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/controller.h"
#include "input/tuning.h"
#include "util/log.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>

#include "platform/platform.h"

#if defined(XCLOUD_PS5)

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
}

namespace xc::input {

namespace {

// A rumble request: packed as a << 8 | b, and when it ends (0 = never).
struct Rumble {
    std::atomic<uint32_t> wanted{0};
    std::atomic<uint64_t> untilMs{0};
    uint32_t applied = 0;

    void set(uint8_t a, uint8_t b, uint32_t durationMs) {
        untilMs = durationMs ? platform::nowMs() + durationMs : 0;
        wanted = (uint32_t(a) << 8) | b;
    }
    // The value wanted now (0 once its time is up).
    uint32_t current() {
        uint32_t want = wanted;
        uint64_t until = untilMs;
        if (want && until && platform::nowMs() >= until) {
            wanted.compare_exchange_strong(want, 0);
            want = 0;
        }
        return want;
    }
    // The value to apply now, or false when it hasn't changed.
    bool due(uint32_t& out) {
        uint32_t want = wanted;
        uint64_t until = untilMs;
        if (want && until && platform::nowMs() >= until) {
            wanted.compare_exchange_strong(want, 0);
            want = 0;
        }
        if (want == applied) return false;
        applied = out = want;
        return true;
    }
};

struct Pad {
    int32_t userId = -1;
    int32_t handle = -1;
    std::atomic<bool> connected{false};  // the last poll's answer
    char name[32] = {};                   // the user's, read when opened
    Rumble motors, triggers;
    TriggerCommand triggerSent[2];  // L2, R2 as last told
    bool triggerFresh = false;      // triggerSent is what the pad has
    bool triggerVibrating[2] = {false, false};
    // Light bar: wanted colour (r << 16 | g << 8 | b, bit 24 = set) and the
    // one shown, easing towards it.
    std::atomic<uint32_t> lightWanted{0};
    uint32_t lightApplied = 0;
    float light[3] = {0, 0, 0};
    bool lightStarted = false;
};
Pad g_pads[kMaxPads];
std::atomic<float> g_deadzone[2] = {0.15f, 0.15f};
std::atomic<bool> g_circleConfirms{false};
std::atomic<int> g_triggerStrength{kTriggerMedium}, g_triggerHz{1}, g_triggerResistance{0};
std::atomic<bool> g_triggerPulses{false}, g_triggerLogging{false};
bool g_rumbleLogged = false;

inline float normStick(uint8_t val) {
    // 0..255 -> -1.0 .. 1.0
    return std::clamp((static_cast<float>(val) - 128.0f) / 128.0f, -1.0f, 1.0f);
}

inline float normTrigger(uint8_t val) {
    return static_cast<float>(val) / 255.0f;
}

bool openPad(Pad& pad, int32_t userId, int slot) {
    int32_t h = scePadOpen(userId, 0, 0, nullptr);
    if (h < 0) {
        XC_LOGW("scePadOpen(user %d): 0x%08x", userId, static_cast<unsigned>(h));
        return false;
    }
    // The DualSense starts in haptics mode, where scePadSetVibration succeeds
    // and does nothing; mode 2 is classic two-motor rumble (as ProsperoEden).
    int mode = scePadSetVibrationMode(h, 2);
    pad.userId = userId;
    pad.handle = h;
    if (sceUserServiceGetUserName(userId, pad.name, sizeof pad.name) != 0) pad.name[0] = 0;
    pad.motors.applied = pad.triggers.applied = 0;
    pad.triggerFresh = false;
    XC_LOGI("pad %d opened (handle %d, rumble mode 0x%08x)", slot, h, static_cast<unsigned>(mode));
    return true;
}

void closePad(Pad& pad, int slot) {
    if (pad.handle < 0) return;
    ScePadVibrationParam off{0, 0};
    scePadSetVibration(pad.handle, &off);
    ScePadTriggerEffectParam none{};
    none.triggerMask = 3;
    scePadSetTriggerEffect(pad.handle, &none);
    if (pad.lightApplied) scePadResetLightBar(pad.handle);
    pad.lightApplied = 0;
    pad.lightStarted = false;
    scePadClose(pad.handle);
    XC_LOGI("pad %d closed (user %d)", slot, pad.userId);
    pad.handle = -1;
    pad.userId = -1;
    pad.connected = false;
    pad.name[0] = 0;
}

void applyLightBar(Pad& pad) {
    uint32_t want = pad.lightWanted;
    if (!(want & (1u << 24))) {
        if (pad.lightApplied) {  // reset: the system's colour again
            scePadResetLightBar(pad.handle);
            pad.lightApplied = 0;
            pad.lightStarted = false;
        }
        return;
    }
    const float target[3] = {static_cast<float>((want >> 16) & 0xFF), static_cast<float>((want >> 8) & 0xFF),
                             static_cast<float>(want & 0xFF)};
    if (!pad.lightStarted) {
        std::copy(target, target + 3, pad.light);
        pad.lightStarted = true;
    }
    // ~120 polls a second: 4 % of the way each, most of it in ~0.4 s.
    for (int i = 0; i < 3; ++i) pad.light[i] += (target[i] - pad.light[i]) * 0.04f;
    ScePadColor c{static_cast<uint8_t>(std::lround(pad.light[0])), static_cast<uint8_t>(std::lround(pad.light[1])),
                  static_cast<uint8_t>(std::lround(pad.light[2])), 0};
    uint32_t shown = (1u << 24) | (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
    if (shown == pad.lightApplied) return;
    int rc = scePadSetLightBar(pad.handle, &c);
    static bool logged = false;
    if (!logged || rc != 0) XC_LOGI("scePadSetLightBar(%u, %u, %u): 0x%08x", c.r, c.g, c.b, static_cast<unsigned>(rc));
    logged = true;
    pad.lightApplied = shown;
}

void applyRumble(Pad& pad) {
    uint32_t v;
    if (pad.motors.due(v)) {
        ScePadVibrationParam p{static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
        int rc = scePadSetVibration(pad.handle, &p);
        if (!g_rumbleLogged || rc != 0) XC_LOGI("scePadSetVibration(%u, %u): 0x%08x", p.largeMotor, p.smallMotor, rc);
        g_rumbleLogged = true;
    }
    // The triggers, worked out on every poll (the pulses change on their
    // own) and sent when either changes.
    v = pad.triggers.current();
    const uint8_t level[2] = {static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
    int strength = g_triggerStrength, hz = g_triggerHz, resistance = g_triggerResistance;
    bool pulses = g_triggerPulses;
    uint64_t now = platform::nowMs();
    TriggerCommand want[2];
    for (int t = 0; t < 2; ++t) want[t] = triggerCommand(level[t], strength, hz, resistance, pulses, now);
    if (pad.triggerFresh && want[0] == pad.triggerSent[0] && want[1] == pad.triggerSent[1]) return;
    ScePadTriggerEffectParam p{};
    p.triggerMask = 3;
    for (int t = 0; t < 2; ++t) {
        p.command[t].mode = want[t].mode;
        std::copy(want[t].data, want[t].data + 4, p.command[t].data);
    }
    int rc = scePadSetTriggerEffect(pad.handle, &p);
    static int failures = 0;  // a refused effect is sent again each poll: logged a few times
    for (int t = 0; t < 2; ++t) {
        // Vibrating: the motor, or pulses (a feedback while there is a request).
        bool was = pad.triggerFresh && pad.triggerVibrating[t];
        bool is = level[t] && triggerAmplitude(level[t], strength);
        if ((g_triggerLogging && was != is) || (rc != 0 && failures < 6))
            XC_LOGI("trigger %s: %s (mode %d, %u %u %u %u): 0x%08x", t ? "R2" : "L2", is ? "vibrating" : "still", want[t].mode,
                    want[t].data[0], want[t].data[1], want[t].data[2], want[t].data[3], static_cast<unsigned>(rc));
    }
    for (int t = 0; t < 2; ++t) pad.triggerVibrating[t] = level[t] && triggerAmplitude(level[t], strength);
    pad.triggerSent[0] = want[0];
    pad.triggerSent[1] = want[1];
    pad.triggerFresh = rc == 0;
    if (rc != 0) ++failures;
}

} // namespace

void setRumble(uint8_t large, uint8_t small, uint32_t durationMs, int pad) {
    if (pad >= 0 && pad < kMaxPads) g_pads[pad].motors.set(large, small, durationMs);
}

void setTriggerRumble(uint8_t left, uint8_t right, uint32_t durationMs, int pad) {
    if (g_triggerStrength <= 0) left = right = 0;
    if (pad >= 0 && pad < kMaxPads) g_pads[pad].triggers.set(left, right, durationMs);
}

void setCircleConfirms(bool on) { g_circleConfirms = on; }
bool circleConfirms() { return g_circleConfirms; }

void setLightBar(uint8_t r, uint8_t g, uint8_t b, int pad) {
    if (pad >= 0 && pad < kMaxPads) g_pads[pad].lightWanted = (1u << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

void resetLightBar(int pad) {
    if (pad >= 0 && pad < kMaxPads) g_pads[pad].lightWanted = 0;
}

bool padConnected(int index) { return index >= 0 && index < kMaxPads && g_pads[index].connected; }

std::string padUserName(int index) {
    // Main thread, like refreshPads() which changes it.
    return index >= 0 && index < kMaxPads && g_pads[index].handle >= 0 ? g_pads[index].name : "";
}

void setTriggerFeel(int strength, int hzIndex, int resistance, bool pulses) {
    g_triggerStrength = std::clamp(strength, 0, kTriggerStrengths - 1);
    g_triggerHz = std::clamp(hzIndex, 0, 2);
    g_triggerResistance = std::clamp(resistance, 0, kTriggerResistances - 1);
    g_triggerPulses = pulses;
    if (!g_triggerStrength)
        for (Pad& p : g_pads) p.triggers.set(0, 0, 0);
    // The next poll works the triggers out in the new feel.
}

void setTriggerLogging(bool on) { g_triggerLogging = on; }

void setDeadzone(float left, float right) {
    g_deadzone[0] = std::clamp(left, 0.0f, 0.5f);
    g_deadzone[1] = std::clamp(right, 0.0f, 0.5f);
}

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
    if (!openPad(g_pads[0], uid, 0)) return false;
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
        if (g_pads[i].handle >= 0 && !signedIn(g_pads[i].userId)) closePad(g_pads[i], i);
    // Signed in: the first free slot.
    for (int32_t user : list.userId) {
        if (user < 0) continue;
        bool known = false;
        for (const Pad& p : g_pads) known |= p.userId == user;
        if (known) continue;
        for (int i = 1; i < kMaxPads; ++i)
            if (g_pads[i].handle < 0) {
                openPad(g_pads[i], user, i);
                break;
            }
    }
}

void shutdown() {
    for (int i = 0; i < kMaxPads; ++i) closePad(g_pads[i], i);
}

bool poll(ControllerState& out) { return pollPad(0, out); }

bool pollPad(int index, ControllerState& out) {
    out = {};
    if (index < 0 || index >= kMaxPads || g_pads[index].handle < 0) return false;
    Pad& slot = g_pads[index];
    applyRumble(slot);
    applyLightBar(slot);
    ScePadData pad{};
    int rc = scePadReadState(slot.handle, &pad);
    slot.connected = rc == 0 && pad.connected;
    if (!slot.connected) return false;

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
    if (g_circleConfirms) std::swap(out.btnA, out.btnB);

    // Shoulders & Sticks
    out.btnL1 = (b & 0x0400) != 0;
    out.btnR1 = (b & 0x0800) != 0;
    out.btnL3 = (b & 0x0002) != 0;
    out.btnR3 = (b & 0x0004) != 0;

    out.btnOptions  = (b & 0x0008) != 0;
    out.btnTouchpad = (b & 0x100000) != 0;

    // Analog
    out.rawLeftX = out.leftStickX = normStick(pad.leftStick.x);
    out.rawLeftY = out.leftStickY = normStick(pad.leftStick.y);
    out.rawRightX = out.rightStickX = normStick(pad.rightStick.x);
    out.rawRightY = out.rightStickY = normStick(pad.rightStick.y);
    radialDeadzone(out.leftStickX, out.leftStickY, g_deadzone[0].load(std::memory_order_relaxed));
    radialDeadzone(out.rightStickX, out.rightStickY, g_deadzone[1].load(std::memory_order_relaxed));

    out.triggerL2 = normTrigger(pad.l2);
    out.triggerR2 = normTrigger(pad.r2);

    // The DualSense touchpad reports 0..1919 x 0..1079.
    if (pad.touchData.touchNum > 0) {
        out.touching = true;
        out.touchX = std::clamp(pad.touchData.touch[0].x / 1919.0f, 0.0f, 1.0f);
        out.touchY = std::clamp(pad.touchData.touch[0].y / 1079.0f, 0.0f, 1.0f);
    }

    return true;
}

} // namespace xc::input

#else

// Host PC fallback implementation
namespace xc::input {
bool init() { return true; }
void shutdown() {}
bool poll(ControllerState& out) { return pollPad(0, out); }
bool pollPad(int index, ControllerState& out) {
    out = {};
    out.connected = index == 0;
    return out.connected;
}
void refreshPads() {}
void setRumble(uint8_t, uint8_t, uint32_t, int) {}
void setTriggerRumble(uint8_t, uint8_t, uint32_t, int) {}
void setDeadzone(float, float) {}
void setCircleConfirms(bool) {}
bool circleConfirms() { return false; }
void setLightBar(uint8_t, uint8_t, uint8_t, int) {}
void resetLightBar(int) {}
bool padConnected(int index) { return index == 0; }
std::string padUserName(int index) { return index == 0 ? "Player" : ""; }
void setTriggerFeel(int, int, int, bool) {}
void setTriggerLogging(bool) {}
} // namespace xc::input

#endif
