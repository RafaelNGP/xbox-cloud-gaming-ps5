// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/gyro.h"

#include <algorithm>
#include <cmath>

namespace xc::input {

void gyroToStick(const float w[3], const float* accel, int level, GyroState& s, float& x, float& y) {
    // Up: gravity, eased so the hand's own jolts don't swing it, and kept
    // pointing out of the pad's top side (not upside down).
    if (accel) {
        float m = std::sqrt(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
        if (m > 0.5f && m < 1.5f) {  // about 1 g: the pad isn't being shaken
            float sign = accel[1] < 0 ? -1.0f : 1.0f;
            for (int i = 0; i < 3; ++i) s.up[i] += (sign * accel[i] / m - s.up[i]) * 0.1f;
            float n = std::sqrt(s.up[0] * s.up[0] + s.up[1] * s.up[1] + s.up[2] * s.up[2]);
            if (n > 1e-3f)
                for (float& u : s.up) u /= n;
        }
    }
    // The turn about the room's vertical: flat, that is the pad's yaw alone.
    const float yaw = w[0] * s.up[0] + w[1] * s.up[1] + w[2] * s.up[2];
    // Below ~1 degree a second it is the hand shaking or the sensor's noise.
    constexpr float kNoise = 0.015f;
    auto still = [](float v) { return std::abs(v) < kNoise ? 0.0f : v - std::copysign(kNoise, v); };
    // Intensity 5: half a turn a second (~0.5 rad/s, a quick aim) is ~40 %.
    const float scale = 0.16f * std::clamp(level, 1, kGyroLevels);
    // Turning left (yaw +) aims left; tilting the top up (pitch +) aims up.
    float tx = -still(yaw) * scale, ty = -still(w[0]) * scale;
    s.x += (tx - s.x) * 0.5f;
    s.y += (ty - s.y) * 0.5f;
    float m = std::hypot(s.x, s.y);
    if (m < 0.005f) {
        x = y = 0;
        return;
    }
    // The games ignore about the first 10-20 % of the stick: start past it.
    constexpr float kLift = 0.12f;
    float lifted = std::min(1.0f, kLift + (1.0f - kLift) * m);
    x = s.x / m * lifted;
    y = s.y / m * lifted;
}

}  // namespace xc::input
