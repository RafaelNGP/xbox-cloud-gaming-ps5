// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The controller's buttons as the screens draw them: PlayStation glyphs
// (Cross, Circle, Square, Triangle, Options, the touchpad, L1...R3) or, with
// an Xbox or another pad laid out like one on a desktop, Xbox ones (A, B, X,
// Y as coloured letters, Menu, View, LB...RS). The PS5's pads are DualSense:
// it always draws the PlayStation ones.
#pragma once

#include "input/input_ext.h"
#include "ui/canvas.h"

#include <atomic>

namespace xc::ui {

class Font;

enum class GlyphStyle { PlayStation, Xbox };

// The glyphs for a pad of this kind: PlayStation for a DualSense, a
// DualShock 4 or no pad; Xbox for the rest (Xbox, Switch and generic pads,
// whose buttons SDL reports by position, as an Xbox pad's).
GlyphStyle glyphStyleFor(input::PadKind kind);

// The glyphs drawn now: the kind of the pad driving the menus (app_core
// sets it; it never changes on the PS5).
inline std::atomic<GlyphStyle> g_glyphStyle{GlyphStyle::PlayStation};

// A button by its place on the pad: the face buttons south, east, west,
// north are Cross / A, Circle / B, Square / X, Triangle / Y.
enum class PadButton { Cross, Circle, Square, Triangle, Options, Touchpad, L1, R1, L2, R2, L3, R3 };

// Its name: "Cross" / "A", "Options" / "Menu", "Touchpad" / "View", "L1" / "LB",
// "L2" / "LT", "L3" / "LS"...
const char* buttonName(GlyphStyle style, PadButton b);

// A face button's colour: the symbol's (PlayStation) or the letter circle's
// (Xbox: A green, B red, X blue, Y yellow). 0 for the other buttons.
Color faceColor(GlyphStyle style, PadButton b);

// A face button as on the pad, `r` its radius: a PlayStation symbol in its
// colour on a round key, or an Xbox letter on a circle of its colour
// (`letters`: the font for it).
void drawFaceButton(Canvas& c, const Font& letters, GlyphStyle style, PadButton b, float cx, float cy, float r);

// Xbox Menu (three lines) and View (two overlapping windows) on a round
// key of radius `r`, lines in `color`.
void drawXboxMenuButton(Canvas& c, float cx, float cy, float r, Color color);
void drawXboxViewButton(Canvas& c, float cx, float cy, float r, Color color);

}  // namespace xc::ui
