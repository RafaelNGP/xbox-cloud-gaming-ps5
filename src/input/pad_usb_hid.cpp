// SPDX-License-Identifier: GPL-3.0-or-later
// Generic USB HID gamepad parser and descriptor interpreter.
// Ported from OneFamily (ChiricoG/OneFamily, MIT) and AnyPad-PS4 (Vld439 / elmonomalvad0).
#include "input/pad_usb_hid.h"

#include <algorithm>
#include <cstring>

namespace xc::input {

namespace {

struct HidGlobals {
    uint16_t page = 0;
    int32_t lmin = 0;
    int32_t lmax = 0;
    uint32_t size = 0;
    uint32_t count = 0;
    uint8_t rid = 0;
};

int32_t itemValue(const uint8_t* p, int size, bool isSigned) {
    uint32_t v = 0;
    for (int i = 0; i < size; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
    if (isSigned && size > 0 && size < 4 && (v & (1u << (8 * size - 1)))) {
        v |= ~0u << (8 * size);
    }
    return static_cast<int32_t>(v);
}

void putField(GenericHidContext& g, uint32_t usage, uint16_t page, const HidGlobals& gl, uint16_t off) {
    HidField f;
    uint16_t pg = (usage >> 16) ? static_cast<uint16_t>(usage >> 16) : page;
    uint16_t u = static_cast<uint16_t>(usage);
    HidField* slot = nullptr;

    f.present = 1;
    f.rid = gl.rid;
    f.off = off;
    f.size = static_cast<uint16_t>(gl.size);
    f.min = gl.lmin;
    f.max = gl.lmax;

    if (pg == 0x01 && u >= 0x30 && u <= 0x37) {
        slot = &g.axis[AX_X + (u - 0x30)];
    } else if (pg == 0x01 && u == 0x39) {
        slot = &g.axis[AX_HAT];
    } else if (pg == 0x02 && u == 0xC5) {
        slot = &g.axis[AX_BRAKE];
    } else if (pg == 0x02 && u == 0xC4) {
        slot = &g.axis[AX_ACCEL];
    } else if (pg == 0x09 && u >= 1 && u <= kMaxHidButtons) {
        slot = &g.button[u - 1];
        if (static_cast<int>(u) > g.nbuttons) g.nbuttons = static_cast<int>(u);
    } else if (pg == 0x0C && u == 0x223) {
        slot = &g.home;
    } else if (pg == 0x0C && u == 0x224) {
        slot = &g.back;
    }

    if (slot && !slot->present) *slot = f;
}

uint32_t getBits(const uint8_t* d, int len, const HidField* f) {
    if (!f || !f->present || f->size == 0) return 0;
    int bitOff = f->off;
    int byteOff = bitOff / 8;
    int bitInByte = bitOff % 8;
    if (byteOff >= len) return 0;
    uint32_t val = 0;
    int bitsLeft = f->size;
    int destBit = 0;
    while (bitsLeft > 0 && byteOff < len) {
        int take = 8 - bitInByte;
        if (take > bitsLeft) take = bitsLeft;
        uint32_t chunk = (d[byteOff] >> bitInByte) & ((1u << take) - 1);
        val |= chunk << destBit;
        destBit += take;
        bitsLeft -= take;
        byteOff++;
        bitInByte = 0;
    }
    return val;
}

uint8_t toByte(const HidField* f, uint32_t raw) {
    if (!f || f->max <= f->min) return 128;
    int64_t val = static_cast<int64_t>(raw);
    if (f->min < 0 && f->size <= 32) {
        if (raw & (1u << (f->size - 1))) {
            val = static_cast<int64_t>(raw) - (1ULL << f->size);
        }
    }
    if (val < f->min) val = f->min;
    if (val > f->max) val = f->max;
    int64_t scaled = ((val - f->min) * 255) / (f->max - f->min);
    return static_cast<uint8_t>(std::clamp<int64_t>(scaled, 0, 255));
}

void applyBuiltinMapping(GenericHidContext& g, uint16_t vid, uint16_t pid) {
    // Default button mapping
    static const uint32_t kDefaultMap[14] = {
        HID_BTN_SQUARE, HID_BTN_CROSS, HID_BTN_CIRCLE, HID_BTN_TRIANGLE,
        HID_BTN_L1, HID_BTN_R1, HID_BTN_L2, HID_BTN_R2,
        HID_BTN_SHARE, HID_BTN_OPTIONS, HID_BTN_L3, HID_BTN_R3,
        HID_BTN_PS, HID_BTN_TOUCHPAD
    };
    for (int i = 0; i < kMaxHidButtons; ++i) {
        g.buttonMap[i] = (i < 14) ? kDefaultMap[i] : 0;
    }

    g.role[ROLE_LX] = AX_X;
    g.role[ROLE_LY] = AX_Y;

    if (g.axis[AX_RX].present && g.axis[AX_RY].present) {
        g.role[ROLE_RX] = AX_RX;
        g.role[ROLE_RY] = AX_RY;
        if (g.axis[AX_Z].present && !g.axis[AX_BRAKE].present) g.role[ROLE_L2] = AX_Z;
        if (g.axis[AX_RZ].present && !g.axis[AX_ACCEL].present) g.role[ROLE_R2] = AX_RZ;
    } else if (g.axis[AX_Z].present && g.axis[AX_RZ].present) {
        g.role[ROLE_RX] = AX_Z;
        g.role[ROLE_RY] = AX_RZ;
    } else {
        g.role[ROLE_RX] = g.axis[AX_RX].present ? AX_RX : (g.axis[AX_Z].present ? AX_Z : AX_NONE);
        g.role[ROLE_RY] = g.axis[AX_RY].present ? AX_RY : (g.axis[AX_RZ].present ? AX_RZ : AX_NONE);
    }
    if (g.axis[AX_BRAKE].present) g.role[ROLE_L2] = AX_BRAKE;
    if (g.axis[AX_ACCEL].present) g.role[ROLE_R2] = AX_ACCEL;

    for (int i = 0; i < ROLE_N; ++i) g.invert[i] = 0;

    // Logitech DirectInput controllers (F310, F710, RumblePad 2 in D-mode)
    if (vid == 0x046D && (pid == 0xC216 || pid == 0xC219 || pid == 0xC218)) {
        g.buttonMap[0] = HID_BTN_SQUARE;
        g.buttonMap[1] = HID_BTN_CROSS;
        g.buttonMap[2] = HID_BTN_CIRCLE;
        g.buttonMap[3] = HID_BTN_TRIANGLE;
        g.buttonMap[4] = HID_BTN_L1;
        g.buttonMap[5] = HID_BTN_R1;
        g.buttonMap[6] = HID_BTN_L2;
        g.buttonMap[7] = HID_BTN_R2;
        g.buttonMap[8] = HID_BTN_SHARE;
        g.buttonMap[9] = HID_BTN_OPTIONS;
        g.buttonMap[10] = HID_BTN_L3;
        g.buttonMap[11] = HID_BTN_R3;
        g.role[ROLE_RX] = AX_Z;
        g.role[ROLE_RY] = AX_RZ;
        return;
    }

    // ShanWan 2.4G wireless dongles / BSP-G6
    if (vid == 0x2563 && (pid == 0x0119 || pid == 0x0523 || pid == 0x0575)) {
        g.buttonMap[0] = HID_BTN_SQUARE;
        g.buttonMap[1] = HID_BTN_CROSS;
        g.buttonMap[2] = HID_BTN_CIRCLE;
        g.buttonMap[3] = HID_BTN_TRIANGLE;
        g.buttonMap[4] = HID_BTN_L1;
        g.buttonMap[5] = HID_BTN_R1;
        g.buttonMap[6] = HID_BTN_L2;
        g.buttonMap[7] = HID_BTN_R2;
        g.buttonMap[8] = HID_BTN_SHARE;
        g.buttonMap[9] = HID_BTN_OPTIONS;
        g.buttonMap[10] = HID_BTN_L3;
        g.buttonMap[11] = HID_BTN_R3;
        g.role[ROLE_RX] = AX_Z;
        g.role[ROLE_RY] = AX_RZ;
        return;
    }

    // 8BitDo DirectInput / Android controllers (SN30 Pro, Ultimate 2.4G, etc.)
    if (vid == 0x2DC8 && (pid == 0x2100 || pid == 0x2101 || pid == 0x3012 || pid == 0x310B || pid == 0x310A || pid == 0x6012)) {
        g.buttonMap[0] = HID_BTN_CROSS;
        g.buttonMap[1] = HID_BTN_CIRCLE;
        g.buttonMap[2] = 0;
        g.buttonMap[3] = HID_BTN_SQUARE;
        g.buttonMap[4] = HID_BTN_TRIANGLE;
        g.buttonMap[5] = 0;
        g.buttonMap[6] = HID_BTN_L1;
        g.buttonMap[7] = HID_BTN_R1;
        g.buttonMap[8] = HID_BTN_L2;
        g.buttonMap[9] = HID_BTN_R2;
        g.buttonMap[10] = HID_BTN_SHARE;
        g.buttonMap[11] = HID_BTN_OPTIONS;
        g.buttonMap[12] = HID_BTN_PS;
        g.buttonMap[13] = HID_BTN_L3;
        g.buttonMap[14] = HID_BTN_R3;
        return;
    }

    // Generic Android layout (GameSir, Betop, Xiaomi, Razer Kishi, etc.)
    if ((vid == 0xFFFF && (pid == 0x046E || pid == 0x046F || pid == 0x0450)) ||
        (vid == 0x05AC && (pid == 0x022D || pid == 0x056B)) ||
        (vid == 0x2717 && pid == 0x3144) ||
        (vid == 0x1532 && pid == 0x0900)) {
        g.buttonMap[0] = HID_BTN_CROSS;
        g.buttonMap[1] = HID_BTN_CIRCLE;
        g.buttonMap[2] = 0;
        g.buttonMap[3] = HID_BTN_SQUARE;
        g.buttonMap[4] = HID_BTN_TRIANGLE;
        g.buttonMap[5] = 0;
        g.buttonMap[6] = HID_BTN_L1;
        g.buttonMap[7] = HID_BTN_R1;
        g.buttonMap[8] = HID_BTN_L2;
        g.buttonMap[9] = HID_BTN_R2;
        g.buttonMap[10] = HID_BTN_SHARE;
        g.buttonMap[11] = HID_BTN_OPTIONS;
        g.buttonMap[12] = HID_BTN_PS;
        g.buttonMap[13] = HID_BTN_L3;
        g.buttonMap[14] = HID_BTN_R3;
        return;
    }
}

}  // namespace

bool parseHidDescriptor(GenericHidContext& g, const uint8_t* d, size_t n, uint16_t vid, uint16_t pid) {
    if (!d || n == 0) return false;
    g = GenericHidContext{};

    HidGlobals gl{};
    HidGlobals stack[4]{};
    int depth = 0;
    size_t i = 0;
    uint32_t usages[64]{};
    uint32_t umin = 0, umax = 0;
    int nusages = 0, hasRange = 0;
    uint16_t offset[256]{};
    uint16_t outOffset[256]{};

    while (i < n) {
        uint8_t p = d[i];
        int size = p & 0x03;
        if (size == 3) size = 4;
        int type = (p >> 2) & 0x03;
        int tag = (p >> 4) & 0x0F;
        const uint8_t* v = d + i + 1;
        if (i + 1 + static_cast<size_t>(size) > n) break;
        i += 1 + size;

        if (type == 0) {  // Main
            switch (tag) {
                case 8: {  // Input
                    uint16_t* off = &offset[gl.rid];
                    if (gl.rid) *off = std::max<uint16_t>(*off, 8);
                    for (uint32_t k = 0; k < gl.count; ++k) {
                        uint32_t u = 0;
                        if (hasRange) {
                            u = (umin + k <= umax) ? (umin + k) : umax;
                        } else if (k < static_cast<uint32_t>(nusages)) {
                            u = usages[k];
                        } else if (nusages > 0) {
                            u = usages[nusages - 1];
                        }
                        putField(g, u, gl.page, gl, *off);
                        *off += static_cast<uint16_t>(gl.size);
                    }
                    nusages = 0;
                    hasRange = 0;
                    break;
                }
                case 9: {  // Output
                    uint16_t* off = &outOffset[gl.rid];
                    if (gl.rid) *off = std::max<uint16_t>(*off, 8);
                    uint16_t total = static_cast<uint16_t>(gl.size * gl.count);
                    if (!g.outPresent && total >= 8) {
                        g.outPresent = 1;
                        g.outHasIds = gl.rid ? 1 : 0;
                        g.outRid = gl.rid;
                        g.outDataBitOff = *off;
                        g.outBits = total;
                        g.outFieldSize = static_cast<uint8_t>(gl.size);
                    }
                    *off += total;
                    nusages = 0;
                    hasRange = 0;
                    break;
                }
                case 10:  // Collection
                case 12:  // End Collection
                    nusages = 0;
                    hasRange = 0;
                    break;
                default:
                    nusages = 0;
                    hasRange = 0;
                    break;
            }
        } else if (type == 1) {  // Global
            switch (tag) {
                case 0: gl.page = static_cast<uint16_t>(itemValue(v, size, false)); break;
                case 1: gl.lmin = itemValue(v, size, true); break;
                case 2: gl.lmax = itemValue(v, size, true); break;
                case 7: gl.size = static_cast<uint32_t>(itemValue(v, size, false)); break;
                case 8:
                    gl.rid = static_cast<uint8_t>(itemValue(v, size, false));
                    g.hasIds = 1;
                    break;
                case 9: gl.count = static_cast<uint32_t>(itemValue(v, size, false)); break;
                case 10:
                    if (depth < 4) stack[depth++] = gl;
                    break;
                case 11:
                    if (depth > 0) gl = stack[--depth];
                    break;
            }
        } else if (type == 2) {  // Local
            switch (tag) {
                case 0:
                    if (nusages < 64) usages[nusages++] = static_cast<uint32_t>(itemValue(v, size, false));
                    break;
                case 1: umin = static_cast<uint32_t>(itemValue(v, size, false)); hasRange = 1; break;
                case 2: umax = static_cast<uint32_t>(itemValue(v, size, false)); hasRange = 1; break;
            }
        }
    }

    applyBuiltinMapping(g, vid, pid);
    return true;
}

bool parseGenericHidReport(const GenericHidContext& ctx, const uint8_t* rx, size_t len, ControllerState& s) {
    if (!rx || len < 4) return false;

    uint8_t rid = ctx.hasIds ? rx[0] : 0;
    const uint8_t* d = rx;

    uint8_t axes[ROLE_N] = {128, 128, 128, 128, 0, 0};
    uint32_t b = 0;
    bool used = false;
    bool trig[2] = {false, false};

    // Update axes
    for (int i = 0; i < ROLE_N; ++i) {
        if (ctx.role[i] == AX_NONE) continue;
        const HidField* f = &ctx.axis[ctx.role[i]];
        if (!f->present || f->rid != rid) continue;
        uint8_t v = toByte(f, getBits(d, static_cast<int>(len), f));
        axes[i] = ctx.invert[i] ? static_cast<uint8_t>(255 - v) : v;
        if (i == ROLE_L2) trig[0] = true;
        if (i == ROLE_R2) trig[1] = true;
        used = true;
    }

    // Update digital buttons
    for (int i = 0; i < ctx.nbuttons && i < kMaxHidButtons; ++i) {
        const HidField* f = &ctx.button[i];
        if (!f->present || f->rid != rid) continue;
        if (getBits(d, static_cast<int>(len), f)) {
            b |= ctx.buttonMap[i];
        }
        used = true;
    }

    // Update Hat D-Pad
    if (ctx.axis[AX_HAT].present && ctx.axis[AX_HAT].rid == rid) {
        const HidField* f = &ctx.axis[AX_HAT];
        int32_t h = static_cast<int32_t>(getBits(d, static_cast<int>(len), f)) - f->min;
        s.dpadUp = s.dpadDown = s.dpadLeft = s.dpadRight = false;
        if (h >= 0 && h < 8) {
            switch (h) {
                case 0: s.dpadUp = true; break;
                case 1: s.dpadUp = s.dpadRight = true; break;
                case 2: s.dpadRight = true; break;
                case 3: s.dpadDown = s.dpadRight = true; break;
                case 4: s.dpadDown = true; break;
                case 5: s.dpadDown = s.dpadLeft = true; break;
                case 6: s.dpadLeft = true; break;
                case 7: s.dpadUp = s.dpadLeft = true; break;
            }
        }
        used = true;
    }

    // Home / Back
    if (ctx.home.present && ctx.home.rid == rid) {
        if (getBits(d, static_cast<int>(len), &ctx.home)) b |= HID_BTN_PS;
        used = true;
    }
    if (ctx.back.present && ctx.back.rid == rid) {
        if (getBits(d, static_cast<int>(len), &ctx.back)) b |= HID_BTN_SHARE;
        used = true;
    }

    if (!used) {
        return false;
    }

    s.rawLeftX = s.leftStickX = (static_cast<float>(axes[ROLE_LX]) - 128.0f) / 127.5f;
    s.rawLeftY = s.leftStickY = (static_cast<float>(axes[ROLE_LY]) - 128.0f) / 127.5f;
    s.rawRightX = s.rightStickX = (static_cast<float>(axes[ROLE_RX]) - 128.0f) / 127.5f;
    s.rawRightY = s.rightStickY = (static_cast<float>(axes[ROLE_RY]) - 128.0f) / 127.5f;

    s.triggerL2 = trig[0] ? (static_cast<float>(axes[ROLE_L2]) / 255.0f) : ((b & HID_BTN_L2) ? 1.0f : 0.0f);
    s.triggerR2 = trig[1] ? (static_cast<float>(axes[ROLE_R2]) / 255.0f) : ((b & HID_BTN_R2) ? 1.0f : 0.0f);

    s.btnX = (b & HID_BTN_SQUARE) != 0;
    s.btnA = (b & HID_BTN_CROSS) != 0;
    s.btnB = (b & HID_BTN_CIRCLE) != 0;
    s.btnY = (b & HID_BTN_TRIANGLE) != 0;
    s.btnL1 = (b & HID_BTN_L1) != 0;
    s.btnR1 = (b & HID_BTN_R1) != 0;
    s.btnTouchpad = (b & (HID_BTN_SHARE | HID_BTN_TOUCHPAD)) != 0;
    s.btnOptions = (b & HID_BTN_OPTIONS) != 0;
    s.btnL3 = (b & HID_BTN_L3) != 0;
    s.btnR3 = (b & HID_BTN_R3) != 0;
    s.btnNexus = (b & HID_BTN_PS) != 0;

    // OneFamily combo for 12-button controllers without a physical Guide/PS button:
    // Back/Select/Share + Start/Options -> Home / Nexus
    if (!s.btnNexus && s.btnTouchpad && s.btnOptions) {
        s.btnNexus = true;
    }

    s.connected = true;
    return true;
}

bool parseGenericHidFallback(const uint8_t* rx, size_t len, ControllerState& s) {
    if (!rx || len < 5) return false;
    size_t off = 0;
    if (rx[0] <= 4 && len >= 6 && rx[1] >= 80 && rx[1] <= 180) {
        off = 1;  // Report ID
    }
    if (off + 4 > len) return false;

    s.rawLeftX = s.leftStickX = (static_cast<float>(rx[off + 0]) - 128.0f) / 127.5f;
    s.rawLeftY = s.leftStickY = (static_cast<float>(rx[off + 1]) - 128.0f) / 127.5f;
    s.rawRightX = s.rightStickX = (static_cast<float>(rx[off + 2]) - 128.0f) / 127.5f;
    s.rawRightY = s.rightStickY = (static_cast<float>(rx[off + 3]) - 128.0f) / 127.5f;

    if (off + 5 <= len) {
        uint8_t hat = rx[off + 4] & 0x0F;
        s.dpadUp = s.dpadDown = s.dpadLeft = s.dpadRight = false;
        if (hat < 8) {
            switch (hat) {
                case 0: s.dpadUp = true; break;
                case 1: s.dpadUp = s.dpadRight = true; break;
                case 2: s.dpadRight = true; break;
                case 3: s.dpadDown = s.dpadRight = true; break;
                case 4: s.dpadDown = true; break;
                case 5: s.dpadDown = s.dpadLeft = true; break;
                case 6: s.dpadLeft = true; break;
                case 7: s.dpadUp = s.dpadLeft = true; break;
            }
        }
    }

    if (off + 7 <= len) {
        uint16_t btns = static_cast<uint16_t>(rx[off + 5] | (rx[off + 6] << 8));
        s.btnX = (btns & 0x0001) != 0;
        s.btnA = (btns & 0x0002) != 0;
        s.btnB = (btns & 0x0004) != 0;
        s.btnY = (btns & 0x0008) != 0;
        s.btnL1 = (btns & 0x0010) != 0;
        s.btnR1 = (btns & 0x0020) != 0;
        if (btns & 0x0040) s.triggerL2 = 1.0f;
        if (btns & 0x0080) s.triggerR2 = 1.0f;
        s.btnTouchpad = (btns & 0x0100) != 0;
        s.btnOptions = (btns & 0x0200) != 0;
        s.btnL3 = (btns & 0x0400) != 0;
        s.btnR3 = (btns & 0x0800) != 0;
        s.btnNexus = (btns & 0x1000) != 0;

        if (!s.btnNexus && s.btnTouchpad && s.btnOptions) {
            s.btnNexus = true;
        }
    }

    s.connected = true;
    return true;
}

}  // namespace xc::input
