// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The controllers in use, as numbered pad glyphs (home screen, in-game menu).
#pragma once

#include "ui/canvas.h"
#include "ui/font.h"

#include <array>
#include <string>

namespace xc::ui {

struct PadSlot {
    bool connected = false;
    std::string name;  // the PS5 user's
};
using PadSlots = std::array<PadSlot, 4>;

// Each player's colour, as the DualSense light bar shows it: blue, red,
// green, pink.
Color playerColor(int index);

// One pad, `w` wide and w * 2 / 3 high, numbered `index + 1`: in the
// player's colour when connected, dim when not.
void drawPadIcon(Canvas& c, const Font& font, int x, int y, int w, int index, bool connected);

// The four side by side, `gap` apart; returns the width taken.
int drawPadRow(Canvas& c, const Font& font, int x, int y, int w, int gap, const PadSlots& pads);

}  // namespace xc::ui
