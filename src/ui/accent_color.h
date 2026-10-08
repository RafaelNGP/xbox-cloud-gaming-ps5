// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// A picture's dominant colour, for the DualSense light bar.
#pragma once

#include "ui/canvas.h"

namespace xc::ui {

// The most present saturated colour of `img`, brightened for an LED (its
// strongest channel at full). False when the picture has no colour to speak
// of (greys, black and white): the caller keeps its default.
bool dominantColor(const Image& img, Color& out);

}  // namespace xc::ui
