// PSBox Cloud Gaming's mark: a white sphere crossed by two curved swooshes
// that pinch at the centre, on Xbox green. Original artwork (not the Xbox
// logo); drawn with the Canvas so the in-app logo and the PS5 home-screen
// icon (ps5/sce_sys/icon0.png, see `xcloud-cli render-icon`) always match.
#pragma once

#include "ui/canvas.h"
#include "ui/font.h"

namespace xc::ui {

constexpr Color kBrandGreen = rgba(16, 124, 16);

// The sphere of radius `r` centred at (cx, cy), in `color`; the swooshes are
// see-through (whatever is behind shows).
void drawBrandMark(Canvas& c, float cx, float cy, float r, Color color = rgba(255, 255, 255));

// The square app icon (green field, mark, "PSBox" wordmark).
void drawAppIcon(Canvas& c, const Fonts& fonts);

}  // namespace xc::ui
