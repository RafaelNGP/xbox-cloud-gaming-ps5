// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// No controllers (a build without a pad backend): every slot is empty, the
// effects go nowhere, and the settings are kept so their getters answer.
#include "input/controller.h"

#include <algorithm>
#include <atomic>

namespace xc::input {

namespace {
// Kept as the PS5's backend keeps them; nothing reads the dead zones without a pad.
std::atomic<float> g_deadzone[2] = {0.15f, 0.15f};
std::atomic<bool> g_circleConfirms{false};
}  // namespace

bool init() { return true; }
void shutdown() {}
bool poll(ControllerState& out) { return pollPad(0, out); }
bool pollPad(int, ControllerState& out) {
    out = {};
    return false;
}
void refreshPads() {}
bool padConnected(int) { return false; }
uint64_t padActiveAt(int) { return 0; }
std::string padUserName(int) { return {}; }
int32_t padUserId(int) { return -1; }
void setRumble(uint8_t, uint8_t, uint32_t, int) {}
void setTriggerRumble(uint8_t, uint8_t, uint32_t, int) {}
void setDeadzone(float left, float right) {
    g_deadzone[0] = std::clamp(left, 0.0f, 0.5f);
    g_deadzone[1] = std::clamp(right, 0.0f, 0.5f);
}
void setCircleConfirms(bool on) { g_circleConfirms = on; }
bool circleConfirms() { return g_circleConfirms; }
void setTriggerFeel(int, int, int, bool) {}
void setTriggerLogging(bool) {}
void setTriggerResistanceActive(bool) {}
void setLightBar(uint8_t, uint8_t, uint8_t, int) {}
void resetLightBar(int) {}
void setGyro(int, int) {}
void setGyroActive(bool) {}
void setEmbeddedMicActive(bool, int) {}

}  // namespace xc::input
