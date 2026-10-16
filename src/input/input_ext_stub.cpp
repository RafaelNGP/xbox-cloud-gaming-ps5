// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// TEMPORARY (branch m1-core only): input/input_ext.h until the input
// backends define it themselves (branch m1-io). A DualSense with every
// feature in every slot, as the PS5 reports, so every screen shows what it
// did before; the rest does nothing. The integrator deletes this file and
// its ${XC_INPUT_EXT_STUB} references in CMakeLists.txt.
#include "input/input_ext.h"

namespace xc::input {

PadCaps padCaps(int) {
    PadCaps c;
    c.kind = PadKind::DualSense;
    c.rumble = c.triggerRumble = c.adaptiveTriggers = c.lightBar = c.gyro = c.touchpad = true;
    return c;
}
PadCaps anyPadCaps() { return padCaps(0); }
void kbmSetCaptured(bool) {}
void sdlInputInit(SDL_Window*) {}
void sdlInputEvent(const SDL_Event&) {}
void sdlInputShutdown() {}

}  // namespace xc::input
