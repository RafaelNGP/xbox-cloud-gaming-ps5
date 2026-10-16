// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Gyro aiming: turning the DualSense moves the right stick, for the cloud
// games that only know sticks. The turn speed (the pad's yaw and pitch) is
// eased a little and lifted past the games' own stick dead zone, so small
// turns still move the aim.
#pragma once

namespace xc::input {

constexpr int kGyroLevels = 10;  // intensity 1..kGyroLevels
enum GyroMode { kGyroOff, kGyroAlways, kGyroAiming };  // kGyroAiming: while L2 is held

struct GyroState {
    float x = 0, y = 0;  // the push given, eased
};

// `w`: the pad's angular velocity (rad/s; x pitch, y yaw, z roll) -> the
// push to add to the right stick (pad convention: +x right, +y down).
void gyroToStick(const float w[3], int level, GyroState& s, float& x, float& y);

}  // namespace xc::input
