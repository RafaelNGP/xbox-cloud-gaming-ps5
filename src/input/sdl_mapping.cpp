// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/sdl_mapping.h"

#include "input/kbm.h"

#include <algorithm>
#include <cstring>

namespace xc::input {

float sdlStickAxis(int16_t v) { return std::clamp(v / 32767.0f, -1.0f, 1.0f); }
float sdlTriggerAxis(int16_t v) { return std::clamp(v / 32767.0f, 0.0f, 1.0f); }

void sdlAccelToG(const float ms2[3], float g[3]) {
    for (int i = 0; i < 3; ++i) g[i] = ms2[i] / kStandardGravity;
}

uint8_t scancodeToVk(int scancode) {
    if (scancode >= 224 && scancode <= 231) return modifierVk(scancode - 224);
    if (scancode < 0 || scancode > 0xFF) return 0;
    return hidToVk(static_cast<uint16_t>(scancode));
}

uint8_t sdlMouseButtons(uint32_t m) {
    return static_cast<uint8_t>((m & 1u ? 1 : 0) | (m & 4u ? 2 : 0) | (m & 2u ? 4 : 0) | (m & 8u ? 8 : 0) | (m & 16u ? 16 : 0));
}

void playerLightColour(int slot, uint8_t rgb[3]) {
    static constexpr uint8_t kColours[4][3] = {{0, 112, 220}, {220, 50, 50}, {40, 170, 80}, {225, 90, 175}};
    const uint8_t* c = kColours[std::clamp(slot, 0, 3)];
    std::copy(c, c + 3, rgb);
}

void ds5TriggerBlock(const TriggerCommand& c, uint8_t out[kDs5TriggerBlock]) {
    std::memset(out, 0, kDs5TriggerBlock);
    out[0] = 0x05;  // off
    // Zones `position`..9 each at `level` (1..8, 3 bits as level - 1).
    auto zones = [&](uint8_t type, int position, int level) {
        uint16_t active = 0;
        uint32_t levels = 0;
        for (int i = std::clamp(position, 0, 9); i < 10; ++i) {
            active |= static_cast<uint16_t>(1u << i);
            levels |= static_cast<uint32_t>((level - 1) & 7) << (3 * i);
        }
        out[0] = type;
        out[1] = static_cast<uint8_t>(active);
        out[2] = static_cast<uint8_t>(active >> 8);
        for (int b = 0; b < 4; ++b) out[3 + b] = static_cast<uint8_t>(levels >> (8 * b));
    };
    if (c.mode == 1 && c.data[1]) {
        zones(0x21, c.data[0], std::min<int>(c.data[1], 8));  // feedback: resistance from `position` on
    } else if (c.mode == 3 && c.data[1] && c.data[2]) {
        zones(0x26, c.data[0], std::min<int>(c.data[1], 8));  // vibration
        out[9] = c.data[2];
    }
}

void ds5TriggerEffects(const TriggerCommand& left, const TriggerCommand& right, uint8_t out[kDs5EffectsSize]) {
    std::memset(out, 0, kDs5EffectsSize);
    out[0] = 0x04 | 0x08;
    ds5TriggerBlock(right, out + kDs5RightTrigger);
    ds5TriggerBlock(left, out + kDs5LeftTrigger);
}

}  // namespace xc::input
