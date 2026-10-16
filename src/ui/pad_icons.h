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
    std::string name;   // the PS5 user's
    bool keyboard = false;  // a USB keyboard and mouse plays in this slot
};
using PadSlots = std::array<PadSlot, 4>;

// Each player's colour, as the DualSense light bar shows it: blue, red,
// green, pink.
Color playerColor(int index);

// A keyboard, `w` wide (about w / 2.4 tall), its top-left at (x, y): an
// outline and three rows of keys, in `color` (cards, the players' row).
void drawKeyboardGlyph(Canvas& c, int x, int y, int w, Color color);

// One pad, a DualSense `w` wide with its number `index + 1` below
// (padIconHeight(w) in all): in the player's colour when connected, dim
// when not. Its buttons are cut out in `background`, the colour behind it.
void drawPadIcon(Canvas& c, const Font& font, int x, int y, int w, int index, bool connected, Color background);
int padIconHeight(int w);

// The four side by side, `gap` apart; returns the width taken.
// One slot: its pad, or the keyboard and mouse playing in it (`icons`: the
// console's icon font, whose controller and keyboard are used; nullptr: drawn).
void drawSlotIcon(Canvas& c, const Font& font, int x, int y, int w, int index, const PadSlot& slot, Color background,
                  const Font* icons = nullptr);

// `icons`: the console's icon font, for the keyboard (nullptr: drawn).
int drawPadRow(Canvas& c, const Font& font, int x, int y, int w, int gap, const PadSlots& pads, Color background,
               const Font* icons = nullptr);

}  // namespace xc::ui
