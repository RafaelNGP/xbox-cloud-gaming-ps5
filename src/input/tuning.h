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

}  // namespace xc::input
