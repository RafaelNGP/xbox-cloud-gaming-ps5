// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// No controllers (a build without a pad backend): every slot is empty, so
// the shared code (controller_common.cpp) reads nothing and sends no effect;
// the settings it keeps (dead zones, confirm button...) still answer.
#include "input/controller.h"
#include "input/controller_backend.h"
#include "input/input_ext.h"

namespace xc::input {

namespace backend {

bool padOpen(int) { return false; }
bool readPad(int, RawPad&) { return false; }
void setMotors(int, uint8_t, uint8_t, uint32_t) {}
TriggerStyle triggerStyle(int) { return TriggerStyle::None; }
int setTriggerEffects(int, const TriggerCommand*) { return -1; }
void setTriggerMotors(int, uint8_t, uint8_t, uint32_t) {}
void setLight(int, uint8_t, uint8_t, uint8_t) {}
void resetLight(int) {}

}  // namespace backend

bool init() { return true; }
void shutdown() {}
bool poll(ControllerState& out) { return pollPad(0, out); }
void refreshPads() {}
std::string padUserName(int) { return {}; }
std::string padDeviceName(int) { return {}; }
int32_t padUserId(int) { return -1; }
void setEmbeddedMicActive(bool, int) {}
PadCaps padCaps(int) { return {}; }
PadCaps anyPadCaps() { return {}; }

}  // namespace xc::input
