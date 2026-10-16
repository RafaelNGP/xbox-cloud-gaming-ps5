// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/gyro.h"

#include <algorithm>
#include <cmath>

namespace xc::input {

void gyroToStick(const float w[3], int level, GyroState& s, float& x, float& y) {
    // Below ~1 degree a second it is the hand shaking or the sensor's noise.
    constexpr float kNoise = 0.015f;
    auto still = [](float v) { return std::abs(v) < kNoise ? 0.0f : v - std::copysign(kNoise, v); };
    // Intensity 5: half a turn a second (~0.5 rad/s, a quick aim) is ~40 %.
    const float scale = 0.16f * std::clamp(level, 1, kGyroLevels);
    // Turning left (yaw +) aims left; tilting the top up (pitch +) aims up.
    float tx = -still(w[1]) * scale, ty = -still(w[0]) * scale;
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
