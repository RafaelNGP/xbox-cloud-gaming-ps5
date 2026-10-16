// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Which device the button hints show: the controller's glyphs, or the
// keyboard's keys once a keyboard (or mouse) is used, until the controller
// is used again.
#pragma once

#include <atomic>

namespace xc::ui {

inline std::atomic<bool> g_keyboardHints{false};

// The key each controller glyph stands for, as the hints show it.
inline const char* keyLabel(int icon) {
    // As app_ui.cpp's Icon: Cross, Circle, Options, Triangle, Touchpad, Square, R3, L2 R2.
    static const char* const kLabels[] = {"Enter", "Esc", "Tab", "F", "Del", "H", "R", "Z C"};
    return icon >= 0 && icon < 8 ? kLabels[icon] : "";
}

}  // namespace xc::ui
