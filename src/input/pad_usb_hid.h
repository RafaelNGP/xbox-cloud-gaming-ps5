// SPDX-License-Identifier: GPL-3.0-or-later
// Generic USB HID gamepad parser and descriptor interpreter.
// Ported from OneFamily (ChiricoG/OneFamily, MIT) and AnyPad-PS4 (Vld439 / elmonomalvad0).
#pragma once

#include "input/controller.h"

#include <cstddef>
#include <cstdint>

namespace xc::input {

enum {
    AX_X = 0,
    AX_Y,
    AX_Z,
    AX_RX,
    AX_RY,
    AX_RZ,
    AX_SLIDER,
    AX_DIAL,
    AX_BRAKE,
    AX_ACCEL,
    AX_HAT,
    AX_N,
    AX_NONE = 0xFF
};

enum {
    ROLE_LX = 0,
    ROLE_LY,
    ROLE_RX,
    ROLE_RY,
    ROLE_L2,
    ROLE_R2,
    ROLE_N
};

constexpr int kMaxHidButtons = 32;

enum {
    HID_BTN_SQUARE   = 0x0001,
    HID_BTN_CROSS    = 0x0002,
    HID_BTN_CIRCLE   = 0x0004,
    HID_BTN_TRIANGLE = 0x0008,
    HID_BTN_L1       = 0x0010,
    HID_BTN_R1       = 0x0020,
    HID_BTN_L2       = 0x0040,
    HID_BTN_R2       = 0x0080,
    HID_BTN_SHARE    = 0x0100,
    HID_BTN_OPTIONS  = 0x0200,
    HID_BTN_L3       = 0x0400,
    HID_BTN_R3       = 0x0800,
    HID_BTN_PS       = 0x1000,
    HID_BTN_TOUCHPAD = 0x2000
};

struct HidField {
    uint8_t present = 0;
    uint8_t rid = 0;
    uint16_t off = 0;
    uint16_t size = 0;
    int32_t min = 0;
    int32_t max = 0;
};

struct GenericHidContext {
    int hasIds = 0;
    HidField axis[AX_N]{};
    HidField button[kMaxHidButtons]{};
    HidField home{};
    HidField back{};
    int nbuttons = 0;

    uint8_t role[ROLE_N]{};
    uint8_t invert[ROLE_N]{};
    uint32_t buttonMap[kMaxHidButtons]{};

    uint8_t outPresent = 0;
    uint8_t outHasIds = 0;
    uint8_t outRid = 0;
    uint16_t outBits = 0;
    uint16_t outDataBitOff = 0;
    uint8_t outFieldSize = 0;
};

// Parses a raw USB HID Report Descriptor and configures the context with standard/builtin mappings.
bool parseHidDescriptor(GenericHidContext& ctx, const uint8_t* desc, size_t len, uint16_t vid, uint16_t pid);

// Decodes an incoming HID interrupt report into ControllerState.
bool parseGenericHidReport(const GenericHidContext& ctx, const uint8_t* data, size_t len, ControllerState& state);

// Fallback for unconfigured HID devices (interprets standard raw reports).
bool parseGenericHidFallback(const uint8_t* data, size_t len, ControllerState& state);

}  // namespace xc::input
