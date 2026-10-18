// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// What every controller backend shares (controller.h): the effects asked
// for (rumble with its timeout, the triggers in the user's feel, the light
// bar easing to its colour) and each pad's state worked out from what the
// backend read (the confirm button swap, the dead zones, gyro aiming, when
// it was last used). The backend (controller_ps5.cpp, controller_sdl.cpp,
// controller_null.cpp) opens the pads and talks to them through
// controller_backend.h.
#include "input/controller.h"
#include "input/controller_backend.h"
#include "input/gyro.h"
#include "input/tuning.h"
#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <atomic>
#include <cmath>

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
    // What is left of it, ms (0: until changed).
    uint32_t remainingMs() const {
        uint64_t until = untilMs, now = platform::nowMs();
        return until ? static_cast<uint32_t>(until > now ? std::min<uint64_t>(until - now, 0xFFFFFFFFu) : 1) : 0;
    }
};

struct Pad {
    std::atomic<bool> connected{false};  // the last poll's answer
    Rumble motors, triggers;
    TriggerCommand triggerSent[2];  // L2, R2 as last told
    bool triggerFresh = false;      // triggerSent is what the pad has
    bool triggerVibrating[2] = {false, false};
    uint32_t triggerMotorsSent = 0;  // TriggerStyle::Motors: L2 << 8 | R2 as last told
    // Light bar: wanted colour (r << 16 | g << 8 | b, bit 24 = set) and the
    // one shown, easing towards it.
    std::atomic<uint32_t> lightWanted{0};
    uint32_t lightApplied = 0;
    float light[3] = {0, 0, 0};
    bool lightStarted = false;
    std::atomic<uint64_t> activeAt{0};  // the last poll with a button or stick in use (ms)
    GyroState gyro;  // gyro aiming's eased push
};
Pad g_pads[kMaxPads];
std::atomic<float> g_deadzone[2] = {0.15f, 0.15f};
std::atomic<bool> g_circleConfirms{false};
std::atomic<int> g_triggerStrength{kTriggerMedium}, g_triggerHz{1}, g_triggerResistance{0};
std::atomic<bool> g_triggerPulses{false}, g_triggerLogging{false}, g_resistanceActive{false};
std::atomic<int> g_triggerProfile{kTriggerProfileStandard};
std::atomic<int> g_gyroMode{kGyroOff}, g_gyroLevel{5};
std::atomic<bool> g_gyroActive{false};
int g_gyroLogged = 0, g_gyroLoggedX = 0;  // the first turns up / down and sideways, to check the axes

void applyLightBar(int index, Pad& pad) {
    uint32_t want = pad.lightWanted;
    if (!(want & (1u << 24))) {
        if (pad.lightApplied) {  // reset: the system's colour again
            backend::resetLight(index);
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
    const uint8_t r = static_cast<uint8_t>(std::lround(pad.light[0])), g = static_cast<uint8_t>(std::lround(pad.light[1])),
                  b = static_cast<uint8_t>(std::lround(pad.light[2]));
    uint32_t shown = (1u << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
    if (shown == pad.lightApplied) return;
    backend::setLight(index, r, g, b);
    pad.lightApplied = shown;
}

// Trigger motors (an Xbox pad's): the game's levels in the user's strength,
// sent when they change.
void applyTriggerMotors(int index, Pad& pad) {
    uint32_t v = pad.triggers.current();
    int strength = g_triggerStrength;
    const uint8_t level[2] = {static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
    const uint8_t out[2] = {triggerMotorLevel(level[0], strength), triggerMotorLevel(level[1], strength)};
    uint32_t packed = (uint32_t(out[0]) << 8) | out[1];
    if (packed == pad.triggerMotorsSent) return;
    if (g_triggerLogging)
        for (int t = 0; t < 2; ++t)
            if (bool is = out[t] != 0; is != (((pad.triggerMotorsSent >> (t ? 0 : 8)) & 0xFF) != 0))
                XC_LOGI("trigger %s: %s (motor %u)", t ? "R2" : "L2", is ? "vibrating" : "still", out[t]);
    backend::setTriggerMotors(index, out[0], out[1], packed ? pad.triggers.remainingMs() : 0);
    pad.triggerMotorsSent = packed;
}

void applyRumble(int index, Pad& pad) {
    uint32_t v;
    if (pad.motors.due(v))
        backend::setMotors(index, static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v), v ? pad.motors.remainingMs() : 0);
    TriggerStyle style = backend::triggerStyle(index);
    if (style == TriggerStyle::Motors) return applyTriggerMotors(index, pad);
    if (style != TriggerStyle::Adaptive) return;
    // The triggers, worked out on every poll (the pulses change on their
    // own) and sent when either changes.
    v = pad.triggers.current();
    const uint8_t level[2] = {static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
    int strength = g_triggerStrength, hz = g_triggerHz, resistance = g_resistanceActive ? g_triggerResistance.load() : 0;
    bool pulses = g_triggerPulses;
    int profile = g_resistanceActive ? g_triggerProfile.load() : kTriggerProfileStandard;
    uint64_t now = platform::nowMs();
    TriggerCommand want[2];
    for (int t = 0; t < 2; ++t) want[t] = triggerCommand(t, level[t], strength, hz, resistance, pulses, profile, now);
    if (pad.triggerFresh && want[0] == pad.triggerSent[0] && want[1] == pad.triggerSent[1]) return;
    int rc = backend::setTriggerEffects(index, want);
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

}  // namespace

bool inUse(const ControllerState& s) {
    return s.btnA || s.btnB || s.btnX || s.btnY || s.dpadUp || s.dpadDown || s.dpadLeft || s.dpadRight || s.btnL1 ||
           s.btnR1 || s.btnL3 || s.btnR3 || s.btnOptions || s.btnTouchpad || s.triggerL2 > 0.5f || s.triggerR2 > 0.5f ||
           std::abs(s.leftStickX) > 0.5f || std::abs(s.leftStickY) > 0.5f || std::abs(s.rightStickX) > 0.5f ||
           std::abs(s.rightStickY) > 0.5f;
}

void slotOpened(int slot) {
    Pad& pad = g_pads[slot];
    pad.motors.applied = pad.triggers.applied = 0;
    pad.triggerFresh = false;
    pad.triggerMotorsSent = 0;
}

bool slotLightSet(int slot) { return g_pads[slot].lightApplied != 0; }

void slotClosed(int slot) {
    Pad& pad = g_pads[slot];
    pad.lightApplied = 0;
    pad.lightStarted = false;
    pad.connected = false;
}

void setRumble(uint8_t large, uint8_t small, uint32_t durationMs, int pad) {
    if (pad >= 0 && pad < kMaxPads) g_pads[pad].motors.set(large, small, durationMs);
}

void setAudioHaptics(uint8_t kick, uint32_t durationMs, int pad) {
    if (pad >= 0 && pad < kMaxPads && kick > 0) {
        Pad& p = g_pads[pad];
        if (p.motors.current() == 0) {
            p.motors.set(kick, 0, durationMs);
        }
    }
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

void setTriggerFeel(int strength, int hzIndex, int resistance, bool pulses, int profile) {
    g_triggerStrength = std::clamp(strength, 0, kTriggerStrengths - 1);
    g_triggerHz = std::clamp(hzIndex, 0, 2);
    g_triggerResistance = std::clamp(resistance, 0, kTriggerResistances - 1);
    g_triggerPulses = pulses;
    g_triggerProfile = std::clamp(profile, 0, kTriggerProfiles - 1);
    if (!g_triggerStrength)
        for (Pad& p : g_pads) p.triggers.set(0, 0, 0);
    // The next poll works the triggers out in the new feel.
}

void setTriggerProfile(int profile) {
    g_triggerProfile = std::clamp(profile, 0, kTriggerProfiles - 1);
}

int triggerProfile() {
    return g_triggerProfile.load();
}

void setTriggerLogging(bool on) { g_triggerLogging = on; }
void setTriggerResistanceActive(bool on) { g_resistanceActive = on; }

void setDeadzone(float left, float right) {
    g_deadzone[0] = std::clamp(left, 0.0f, 0.5f);
    g_deadzone[1] = std::clamp(right, 0.0f, 0.5f);
}

bool pollPad(int index, ControllerState& out) {
    out = {};
    if (index < 0 || index >= kMaxPads || !backend::padOpen(index)) return false;
    Pad& slot = g_pads[index];
    applyRumble(index, slot);
    applyLightBar(index, slot);
    RawPad pad;
    slot.connected = backend::readPad(index, pad);
    if (!slot.connected) return false;

    out.connected = true;
    out.dpadUp = pad.dpadUp;
    out.dpadRight = pad.dpadRight;
    out.dpadDown = pad.dpadDown;
    out.dpadLeft = pad.dpadLeft;

    out.btnA = pad.btnA;
    out.btnB = pad.btnB;
    out.btnX = pad.btnX;
    out.btnY = pad.btnY;
    if (g_circleConfirms) std::swap(out.btnA, out.btnB);

    out.btnL1 = pad.btnL1;
    out.btnR1 = pad.btnR1;
    out.btnL3 = pad.btnL3;
    out.btnR3 = pad.btnR3;

    out.btnOptions = pad.btnOptions;
    out.btnTouchpad = pad.btnTouchpad;
    out.btnNexus = pad.btnNexus;

    out.rawLeftX = out.leftStickX = pad.leftX;
    out.rawLeftY = out.leftStickY = pad.leftY;
    out.rawRightX = out.rightStickX = pad.rightX;
    out.rawRightY = out.rightStickY = pad.rightY;
    radialDeadzone(out.leftStickX, out.leftStickY, g_deadzone[0].load(std::memory_order_relaxed));
    radialDeadzone(out.rightStickX, out.rightStickY, g_deadzone[1].load(std::memory_order_relaxed));

    out.triggerL2 = pad.l2;
    out.triggerR2 = pad.r2;

    // Gyro aiming: in a game (menu closed), always or while L2 aims.
    int gyroMode = g_gyroMode.load(std::memory_order_relaxed);
    bool gyroOn = g_gyroActive && (gyroMode == kGyroAlways || (gyroMode == kGyroAiming && out.triggerL2 > 0.3f));
    if (gyroOn) {
        float gx, gy;
        gyroToStick(pad.gyro, pad.hasAccel ? pad.accel : nullptr, g_gyroLevel.load(std::memory_order_relaxed), slot.gyro, gx, gy);
        float x = out.rightStickX + gx, y = out.rightStickY + gy, m = std::hypot(x, y);
        if (m > 1.0f) x /= m, y /= m;
        out.rightStickX = x;
        out.rightStickY = y;
        // The first few turns each way, to check the axes.
        bool sideways = std::abs(gx) > 0.5f && std::abs(gx) > std::abs(gy);
        int& logged = sideways ? g_gyroLoggedX : g_gyroLogged;
        if (logged < 4 && std::max(std::abs(gx), std::abs(gy)) > 0.5f)
            XC_LOGI("gyro: pitch %.2f yaw %.2f roll %.2f rad/s, gravity %.2f,%.2f,%.2f -> stick %.2f,%.2f", pad.gyro[0],
                    pad.gyro[1], pad.gyro[2], pad.accel[0], pad.accel[1], pad.accel[2], gx, gy), ++logged;
    } else {
        slot.gyro = {};
    }

    if (pad.touching) {
        out.touching = true;
        out.touchSecond = pad.touchSecond;
        out.touchX = pad.touchX;
        out.touchY = pad.touchY;
    }
    if (inUse(out)) slot.activeAt = platform::nowMs();

    return true;
}

uint64_t padActiveAt(int index) { return index >= 0 && index < kMaxPads ? g_pads[index].activeAt.load() : 0; }
void setGyro(int mode, int level) {
    g_gyroMode = std::clamp(mode, 0, 2);
    g_gyroLevel = std::clamp(level, 1, kGyroLevels);
}

void setGyroActive(bool on) { g_gyroActive = on; }

}  // namespace xc::input
