// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// How the in-app notices (platform/toasts.h: platform::notify() in the
// desktop app) are drawn: over the menus, and over a game as its overlay.
#pragma once

#include "ui/canvas.h"
#include "ui/font.h"

#include <string>
#include <vector>

namespace xc::ui {

// A rounded panel per notice, stacked down from `top`, right-aligned to
// `right`; nothing when `texts` is empty.
void drawToasts(Canvas& c, const Fonts& fonts, const std::vector<std::string>& texts, int right, int top);
// The notices in one panel of their own, for display::setOverlay() while a
// game streams (`texts` not empty).
Canvas renderToastPanel(const Fonts& fonts, const std::vector<std::string>& texts);

}  // namespace xc::ui
