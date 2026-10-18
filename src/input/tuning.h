// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// How the stick dead zone and the trigger vibration are worked out: shared
// by the controller code, the Settings testers that show them, and the tests.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace xc::input {

// A radial dead zone with rescaling: nothing while the stick is within
// `deadzone` (0..1) of the centre, then 0..1 over the rest of the travel, in
// the direction the stick points (no axis snaps to zero near the other).
inline void radialDeadzone(float& x, float& y, float deadzone) {
    float m = std::hypot(x, y);
    if (m <= deadzone || m <= 0) {
        x = y = 0;
        return;
    }
    float k = std::min(1.0f, (m - deadzone) / (1.0f - deadzone)) / m;
    x = std::clamp(x * k, -1.0f, 1.0f);
    y = std::clamp(y * k, -1.0f, 1.0f);
}

// Trigger vibration strengths (Settings): off, light, medium, strong, max.
constexpr int kTriggerStrengths = 5;
constexpr int kTriggerMedium = 2;
// The vibration's frequency choices, Hz: low (a thump), medium, high (a buzz).
constexpr int kTriggerHz[] = {30, 60, 120};

// Xbox impulse-trigger level (0..255) -> DualSense trigger amplitude (0..8).
// Games ask for little (Halo's shots: 15 %), which the Xbox's trigger motors
// turn into a clear kick; linearly it would be amplitude 1, which drowns
// under the grip motors. A square-root curve: 15 % -> 4, 50 % -> 6 at medium
// strength; light halves it, strong is 1.5 times, max always 8.
inline uint8_t triggerAmplitude(uint8_t level, int strength = kTriggerMedium) {
    if (!level || strength <= 0) return 0;
    if (strength >= 4) return 8;
    static constexpr float kScale[] = {0, 0.5f, 1.0f, 1.5f};
    float a = std::ceil(8.0f * std::sqrt(level / 255.0f));
    a = std::max(a, 2.0f) * kScale[strength];
    return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(a)), 1, 8));
}

// The same request for a pad with trigger motors (an Xbox pad on the
// desktop): the game's level as it is at medium strength, since it was
// made for these motors; light halves it, strong is 1.5 times, max full.
inline uint8_t triggerMotorLevel(uint8_t level, int strength = kTriggerMedium) {
    if (!level || strength <= 0) return 0;
    if (strength >= 4) return 255;
    static constexpr float kScale[] = {0, 0.5f, 1.0f, 1.5f};
    int v = static_cast<int>(std::lround(level * kScale[strength]));
    return static_cast<uint8_t>(std::clamp(v, 1, 255));
}

// Resistance (the triggers' weight): off, light, medium, strong. The
// DualSense pushes back from a point of the travel on, earlier and harder
// the stronger it is; the Xbox's triggers have none, so it is the user's
// choice. (The console takes the trigger modes 0..3 only: the slope and
// per-position modes of Sony's PC headers are refused, 0x80920001.)
constexpr int kTriggerResistances = 4;

// What one trigger is told to do (ScePadTriggerEffectCommand: its mode and
// the first bytes of its parameters, the rest zero).
struct TriggerCommand {
    int32_t mode = 0;  // 0 off, 1 feedback (resistance), 3 vibration
    uint8_t data[4] = {0, 0, 0, 0};
    bool operator==(const TriggerCommand& o) const {
        return mode == o.mode && data[0] == o.data[0] && data[1] == o.data[1] && data[2] == o.data[2] && data[3] == o.data[3];
    }
    bool operator!=(const TriggerCommand& o) const { return !(*this == o); }
};

// The pulses' rate for a frequency choice: the pad is updated ~120 times a
// second, so the push / release can't go faster than this.
constexpr int kTriggerPulseHz[] = {10, 20, 30};

// Trigger feel profiles:
// 0: Standard (user vibration, frequency, resistance, and pulses)
// 1: Shooter (L2 Hair Trigger instant aim snap, R2 Weapon Trigger Break wall)
// 2: Racing (L2 Hydraulic ABS brake lockup pulses, R2 Progressive Throttle)
constexpr int kTriggerProfileStandard = 0;
constexpr int kTriggerProfileShooter = 1;
constexpr int kTriggerProfileRacing = 2;
constexpr int kTriggerProfiles = 3;

// One trigger at a moment: `level` the game's request (0..255), and the
// user's strength, frequency choice (index), resistance, and style: the
// motor vibrating (resistance gone while it does), or pulses of the
// resistance itself (pushing harder and letting go at kTriggerPulseHz),
// which keep the weight.
inline TriggerCommand triggerCommand(int triggerIndex, uint8_t level, int strength, int hzIndex, int resistance, bool pulses, int profile, uint64_t nowMs) {
    if (profile == kTriggerProfileShooter) {
        TriggerCommand c;
        uint8_t amplitude = triggerAmplitude(level, strength);
        if (triggerIndex == 0) {
            // L2 (Aim Down Sight): Hair Trigger snap with instant light resistance at position 1
            if (amplitude) {
                c.mode = 3;
                c.data[1] = amplitude;
                c.data[2] = static_cast<uint8_t>(kTriggerHz[hzIndex]);
            } else {
                c.mode = 1;
                c.data[0] = 1;  // start position 1
                c.data[1] = 2;  // force 2 (light aim wall)
            }
        } else {
            // R2 (Weapon Trigger): Mechanical Trigger Break wall at position 2
            if (amplitude) {
                c.mode = 3;
                c.data[1] = std::max<uint8_t>(amplitude, 6);
                c.data[2] = 60;  // 60 Hz weapon recoil
            } else {
                c.mode = 1;
                c.data[0] = 2;  // start position 2
                c.data[1] = 5;  // force 5 (firm trigger wall)
            }
        }
        return c;
    }
    if (profile == kTriggerProfileRacing) {
        TriggerCommand c;
        uint8_t amplitude = triggerAmplitude(level, strength);
        if (triggerIndex == 0) {
            // L2 (Brake Pedal): ABS Hydraulic Pulsing when braking hard (level > 0)
            if (amplitude || level > 30) {
                bool push = (nowMs * 25 * 2 / 1000) % 2 == 0;
                c.mode = 1;
                c.data[0] = 1;
                c.data[1] = push ? 7 : 2;  // ABS rapid kickback
            } else {
                c.mode = 1;
                c.data[0] = 2;
                c.data[1] = 3;  // moderate brake resistance
            }
        } else {
            // R2 (Gas / Throttle): Smooth progressive throttle resistance
            if (amplitude) {
                c.mode = 3;
                c.data[1] = amplitude;
                c.data[2] = 60;  // traction loss vibration
            } else {
                c.mode = 1;
                c.data[0] = 1;
                c.data[1] = 3;  // throttle resistance
            }
        }
        return c;
    }

    // Standard profile
    static constexpr uint8_t kFeedback[kTriggerResistances][2] = {{0, 0}, {4, 2}, {3, 4}, {2, 6}};
    static constexpr uint8_t kBase[kTriggerResistances] = {0, 2, 3, 4};
    resistance = std::clamp(resistance, 0, kTriggerResistances - 1);
    hzIndex = std::clamp(hzIndex, 0, 2);
    TriggerCommand c;
    uint8_t amplitude = triggerAmplitude(level, strength);
    if (amplitude && !pulses) {
        c.mode = 3;  // vibration: from the top of the travel
        c.data[1] = amplitude;
        c.data[2] = static_cast<uint8_t>(kTriggerHz[hzIndex]);
    } else if (amplitude) {
        bool push = (nowMs * static_cast<uint64_t>(kTriggerPulseHz[hzIndex]) * 2 / 1000) % 2 == 0;
        int s = push ? std::min(8, kBase[resistance] + amplitude) : kBase[resistance];
        if (s) c.mode = 1, c.data[1] = static_cast<uint8_t>(s);  // feedback from position 0
    } else if (resistance) {
        c.mode = 1;  // feedback: the weight from its point on
        c.data[0] = kFeedback[resistance][0], c.data[1] = kFeedback[resistance][1];
    }
    return c;
}

inline TriggerCommand triggerCommand(uint8_t level, int strength, int hzIndex, int resistance, bool pulses, uint64_t nowMs) {
    return triggerCommand(0, level, strength, hzIndex, resistance, pulses, kTriggerProfileStandard, nowMs);
}

}  // namespace xc::input
